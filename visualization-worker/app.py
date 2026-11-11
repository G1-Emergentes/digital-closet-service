"""Outfit visualization worker for Digital Closet.

Puts 1-3 of the caller's garments on the caller's own photo using Cloudflare Workers AI
(FLUX.2 klein). It needs no database of its own: everything is fetched with the caller's
bearer token, so ownership is enforced by the services that own the data:
  - garments come from the Digital Closet service (contract: resolve + image-access)
  - the person photo comes from IAM (avatar_profiles + private bucket)
Jobs live in memory.
"""
import base64
import io
import logging
import os
import re
import time
from concurrent.futures import ThreadPoolExecutor
from threading import Lock
from uuid import UUID, uuid4

import httpx
from fastapi import Body, FastAPI, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse, Response
from PIL import Image

log = logging.getLogger("uvicorn.error")  # shows up in the worker's console
MAX_PHOTO_BYTES = 8 * 1024 * 1024  # same limit as the avatar photo
MAX_GARMENTS = 3  # Cloudflare takes 4 reference images: the person plus three garments
MAX_SIDE = 480  # Cloudflare requires every input image to be smaller than 512x512
MAX_JOBS = 50  # shortcut: in-memory jobs, oldest dropped; persist them if results must survive restarts
MODEL = "@cf/black-forest-labs/flux-2-klein-4b"
FLAGGED = 3030  # Cloudflare error code: "Your output has been flagged"
OUTPUT_SIZE = ("512", "768")

MESSAGES = {
    "INVALID_ARGUMENT": "Parámetros o cuerpo inválidos.",
    "UNAUTHENTICATED": "Token ausente, inválido o sesión rechazada.",
    "GARMENT_NOT_FOUND": "Prenda inexistente o no accesible.",
    "GARMENT_UNAVAILABLE": "Una prenda no está disponible.",
    "GARMENT_WITHOUT_IMAGE": "Una prenda no tiene imagen.",
    "PERSON_PHOTO_NOT_FOUND": "El usuario no tiene foto de avatar.",
    "VISUALIZATION_NOT_FOUND": "Visualización inexistente.",
    "VISUALIZATION_NOT_READY": "La imagen aún no está lista.",
    "SERVICE_UNAVAILABLE": "Dependencia temporalmente no disponible.",
}


class Problem(Exception):
    """Contract error envelope: {"error": {"code", "message"}}."""

    def __init__(self, status, code, message=None):
        self.status, self.code = status, code
        self.message = message or MESSAGES.get(code, "Fallo inesperado.")


def settings():
    # Read on every call so the environment can change between requests (and in tests).
    get = lambda name, default="": os.environ.get(name, default).strip().rstrip("/")
    return {
        "closet": get("DC_API_URL", "http://127.0.0.1:8081/api/v1"),
        "iam": get("IAM_SUPABASE_URL"),
        "iam_key": get("IAM_SUPABASE_PUBLISHABLE_KEY"),
        "cf_token": get("CF_API_TOKEN"),
        "cf_account": get("CF_ACCOUNT_ID"),
    }


def http():
    return httpx.Client(timeout=30.0)


def bearer(authorization):
    if not authorization or not authorization.startswith("Bearer ") or not 7 < len(authorization) <= 8192:
        raise Problem(401, "UNAUTHENTICATED")
    return authorization


def uuid_text(value):
    try:
        return str(UUID(str(value)))
    except ValueError:
        raise Problem(400, "INVALID_ARGUMENT", "Se esperaba un UUID.")


def decode_photo(value):
    """A photo sent by the client is a trust boundary: size, real image content and format."""
    problem = Problem(400, "INVALID_ARGUMENT", "person_photo debe ser una imagen JPEG, PNG o WebP en base64 (máx. 8 MiB, mínimo 256 px).")
    if not isinstance(value, str) or len(value) > MAX_PHOTO_BYTES * 4 // 3 + 64:
        raise problem
    try:
        data = base64.b64decode(value.split(",")[-1], validate=True)
        image = Image.open(io.BytesIO(data))
        image.verify()
    except Exception:
        raise problem
    if image.format not in ("JPEG", "PNG", "WEBP") or min(image.size) < 256 or image.size[0] * image.size[1] > 20_000_000:
        raise problem
    return data


def parse_request(body):
    """Returns (garment ids, recommendation id or None, person photo bytes or None)."""
    ids = body.get("garment_ids") if isinstance(body, dict) else None
    if not isinstance(ids, list) or not 1 <= len(ids) <= MAX_GARMENTS:
        raise Problem(400, "INVALID_ARGUMENT", f"garment_ids debe tener entre 1 y {MAX_GARMENTS} UUID.")
    ids = [uuid_text(i) for i in ids]
    if len(set(ids)) != len(ids):
        raise Problem(400, "INVALID_ARGUMENT", "garment_ids no admite repetidos.")
    recommendation = body.get("recommendation_id")
    photo = body.get("person_photo")
    return (ids, uuid_text(recommendation) if recommendation is not None else None,
            decode_photo(photo) if photo is not None else None)


# ---- who is calling ---------------------------------------------------------------------


def iam_headers(token, cfg):
    return {"apikey": cfg["iam_key"], "Authorization": token}


def iam_user(token):
    """Same two checks as iam-service: Auth knows the user, and the session is still active."""
    cfg = settings()
    if not cfg["iam"] or not cfg["iam_key"]:
        raise Problem(503, "SERVICE_UNAVAILABLE")
    try:
        with http() as client:
            user = client.get(f"{cfg['iam']}/auth/v1/user", headers=iam_headers(token, cfg))
            if user.status_code in (401, 403):
                raise Problem(401, "UNAUTHENTICATED")
            if user.status_code != 200:
                raise Problem(503, "SERVICE_UNAVAILABLE")
            active = client.post(f"{cfg['iam']}/rest/v1/rpc/mirage_session_active", headers=iam_headers(token, cfg), json={})
    except httpx.HTTPError:
        raise Problem(503, "SERVICE_UNAVAILABLE")
    if active.status_code != 200 or active.json() is not True:
        raise Problem(401, "UNAUTHENTICATED")
    return uuid_text(user.json().get("id"))


# ---- inputs -----------------------------------------------------------------------------


def person_photo(token):
    """The user's avatar photo: the row is visible only to its owner (RLS in IAM)."""
    cfg = settings()
    try:
        with http() as client:
            row = client.get(f"{cfg['iam']}/rest/v1/avatar_profiles?select=photo_path", headers=iam_headers(token, cfg))
            if row.status_code != 200:
                raise Problem(503, "SERVICE_UNAVAILABLE")
            rows = row.json()
            if not rows:
                raise Problem(404, "PERSON_PHOTO_NOT_FOUND")
            photo = client.get(f"{cfg['iam']}/storage/v1/object/mirage-avatar-photos/{rows[0]['photo_path']}", headers=iam_headers(token, cfg))
    except httpx.HTTPError:
        raise Problem(503, "SERVICE_UNAVAILABLE")
    if photo.status_code == 404:
        raise Problem(404, "PERSON_PHOTO_NOT_FOUND")
    if photo.status_code != 200:
        raise Problem(503, "SERVICE_UNAVAILABLE")
    return photo.content


def garments_with_images(token, ids):
    """Uses the Digital Closet contract itself: resolve, then image-access per garment."""
    cfg = settings()
    auth = {"Authorization": token}
    try:
        with http() as client:
            resolved = client.post(f"{cfg['closet']}/garments/resolve", headers=auth, json={"ids": ids})
            if resolved.status_code == 401:
                raise Problem(401, "UNAUTHENTICATED")
            if resolved.status_code != 200:
                raise Problem(503, "SERVICE_UNAVAILABLE")
            found = []
            for entry in resolved.json()["results"]:
                if entry["status"] == "NOT_FOUND":
                    raise Problem(404, "GARMENT_NOT_FOUND")
                if entry["status"] == "UNAVAILABLE":
                    raise Problem(409, "GARMENT_UNAVAILABLE")
                found.append(entry["garment"])
            for garment in found:
                access = client.get(f"{cfg['closet']}/garments/{garment['id']}/image-access", headers=auth)
                if access.status_code == 404:
                    raise Problem(422, "GARMENT_WITHOUT_IMAGE")
                if access.status_code != 200:
                    raise Problem(503, "SERVICE_UNAVAILABLE")
                image = client.get(access.json()["url"])
                if image.status_code != 200:
                    raise Problem(503, "SERVICE_UNAVAILABLE")
                garment["_image"] = image.content
    except httpx.HTTPError:
        raise Problem(503, "SERVICE_UNAVAILABLE")
    return found


# ---- generation -------------------------------------------------------------------------


PLAIN_PRINTS = (
    " Render any printed graphic, photo, text or logo on the garments as a simple abstract pattern: "
    "never draw faces or readable text."
)


def build_prompt(garments, plain_prints=False):
    # Only closed-vocabulary fields (category, colours): free-text descriptions are data and
    # must never reach the model as instructions.
    lines = []
    for index, garment in enumerate(garments, start=2):
        colours = "/".join(c.lower() for c in garment["colors"]) or "unspecified colour"
        kind = garment["category"].lower().replace("_", " ")
        lines.append(f"image {index} is a {colours} {kind} garment")
    return (
        "The person from image 1 is now wearing the garments shown in the other images (" + "; ".join(lines) + "). "
        "Keep the exact face, identity, hairstyle, skin tone, body proportions and pose of image 1, and the same plain background. "
        "Change only the clothing items shown in the other images and keep every other item from image 1 as it is. "
        "Reproduce each garment's colors, design and texture faithfully, with realistic fabric folds and lighting, full body in frame."
        + (PLAIN_PRINTS if plain_prints else "")
    )


def small_png(data):
    image = Image.open(io.BytesIO(data)).convert("RGB")
    image.thumbnail((MAX_SIDE, MAX_SIDE))
    out = io.BytesIO()
    image.save(out, format="PNG")
    return out.getvalue()


def longest_string(node):
    # The generated image is the longest string in the response; avoids guessing field names.
    if isinstance(node, str):
        return node
    items = node.values() if isinstance(node, dict) else node if isinstance(node, list) else []
    return max((longest_string(v) for v in items), key=len, default="")


def generate(person, garments):
    cfg = settings()
    if not cfg["cf_token"] or not cfg["cf_account"]:
        raise RuntimeError("PROVIDER_NOT_CONFIGURED")
    images = [person] + [g["_image"] for g in garments]
    # Encode once; the multipart body is rebuilt for a retry.
    encoded = [small_png(data) for data in images]
    for plain in (False, True):
        files = {f"input_image_{i}": (f"{i}.png", png, "image/png") for i, png in enumerate(encoded)}
        response = httpx.post(
            f"https://api.cloudflare.com/client/v4/accounts/{cfg['cf_account']}/ai/run/{MODEL}",
            headers={"Authorization": f"Bearer {cfg['cf_token']}"},
            data={"prompt": build_prompt(garments, plain_prints=plain), "width": OUTPUT_SIZE[0], "height": OUTPUT_SIZE[1]},
            files=files,
            timeout=300.0,
        )
        if response.status_code == 200:
            return base64.b64decode(longest_string(response.json()).split(",")[-1])
        # The reason stays in the server log; clients only see a short code.
        log.warning("cloudflare answered %s: %s", response.status_code, response.text[:300])
        if not is_flagged(response):
            raise RuntimeError("PROVIDER_FAILED")
        # Flagged by the provider's safety filter (often a printed face on a band tee): retry
        # once asking for abstract prints; if that is flagged too, tell the user.
    raise RuntimeError("CONTENT_FLAGGED")


def is_flagged(response):
    try:
        return any(error.get("code") == FLAGGED for error in response.json().get("errors", []))
    except (ValueError, AttributeError):
        return False


class Jobs:
    def __init__(self, runner=generate):
        self.runner = runner
        self.pool = ThreadPoolExecutor(max_workers=1)  # the provider is the bottleneck
        self.lock = Lock()
        self.jobs = {}

    def submit(self, owner, ids, recommendation, person, garments):
        job_id = str(uuid4())
        job = {"id": job_id, "owner": owner, "status": "PENDING", "garment_ids": ids,
               "recommendation_id": recommendation, "error": None, "created_at": time.time(), "image": None}
        with self.lock:
            self.jobs[job_id] = job
            for old in sorted(self.jobs, key=lambda k: self.jobs[k]["created_at"])[:-MAX_JOBS]:
                del self.jobs[old]
        snapshot = dict(job)  # the 202 reply always shows the initial state, not a racing update
        self.pool.submit(self.run, job_id, person, garments)
        return snapshot

    def run(self, job_id, person, garments):
        job = self.jobs.get(job_id)
        if job is None:
            return
        job["status"] = "PROCESSING"
        try:
            job["image"] = self.runner(person, garments)
            job["status"] = "READY"
        except Exception as error:  # the provider's message is never shown to clients
            log.exception("visualization job %s failed", job_id)
            job["status"] = "FAILED"
            known = ("PROVIDER_NOT_CONFIGURED", "PROVIDER_FAILED", "CONTENT_FLAGGED")
            job["error"] = str(error) if str(error) in known else "INTERNAL_ERROR"

    def owned(self, job_id, owner):
        job = self.jobs.get(job_id)
        if job is None or job["owner"] != owner:  # someone else's job looks like a missing one
            raise Problem(404, "VISUALIZATION_NOT_FOUND")
        return job


jobs = Jobs()
app = FastAPI(title="Digital Closet visualization worker")


@app.exception_handler(Problem)
async def problem_handler(_, error):
    return JSONResponse({"error": {"code": error.code, "message": error.message}}, status_code=error.status,
                        headers={"Cache-Control": "no-store"})


@app.exception_handler(RequestValidationError)
async def invalid_body_handler(_, __):
    return JSONResponse({"error": {"code": "INVALID_ARGUMENT", "message": MESSAGES["INVALID_ARGUMENT"]}}, status_code=400)


def public(job):
    return {"id": job["id"], "status": job["status"], "garment_ids": job["garment_ids"],
            "recommendation_id": job["recommendation_id"], "error": job["error"]}


@app.get("/health")
def health():
    return Response("ok", media_type="text/plain")


@app.post("/api/v1/outfit-visualizations", status_code=202)
def create(request: Request, body: dict = Body(...)):
    token = bearer(request.headers.get("authorization"))
    ids, recommendation, uploaded = parse_request(body)
    owner = iam_user(token)
    garments = garments_with_images(token, ids)  # ownership and availability checked by the closet
    # A photo sent with the request wins; otherwise the saved avatar photo from IAM. Neither is stored here.
    person = uploaded if uploaded is not None else person_photo(token)
    return JSONResponse(public(jobs.submit(owner, ids, recommendation, person, garments)), status_code=202,
                        headers={"Cache-Control": "no-store"})


@app.get("/api/v1/outfit-visualizations/{job_id}")
def status(job_id: str, request: Request):
    owner = iam_user(bearer(request.headers.get("authorization")))
    return JSONResponse(public(jobs.owned(uuid_text(job_id), owner)), headers={"Cache-Control": "no-store"})


@app.get("/api/v1/outfit-visualizations/{job_id}/image")
def image(job_id: str, request: Request):
    owner = iam_user(bearer(request.headers.get("authorization")))
    job = jobs.owned(uuid_text(job_id), owner)
    if job["status"] != "READY":
        raise Problem(409, "VISUALIZATION_NOT_READY")
    return Response(job["image"], media_type="image/png", headers={"Cache-Control": "no-store"})
