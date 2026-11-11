# Digital Closet Service

API C++17 con Drogon para el armario digital de Mirage (prendas, imágenes privadas y los
tres endpoints que consume Recommendation). Supabase aporta PostgreSQL, Auth y Storage.

- Diseño, modelo y decisiones: [DIGITAL_CLOSET.md](DIGITAL_CLOSET.md)
- Contrato con Recommendation: `DIGITAL_CLOSET_RECOMMENDATION_CONTRACT.md` v1.0.1
- Migraciones: `supabase/migrations/` (se aplican en la base propia del servicio, no en la de IAM)

## Desarrollo local

Requisitos: Visual Studio con herramientas C++ x64, CMake, Ninja y Drogon con vcpkg
(`x64-windows`). Desde una consola de desarrollo x64:

```powershell
cmake --preset x64-debug -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build out/build/x64-debug-vcpkg
Copy-Item .env.example .env   # completar las cuatro variables
```

El servidor escucha en `http://127.0.0.1:8081` (`/health`, `/api/v1/garments`). Usa dos
proyectos de Supabase: el suyo (`SUPABASE_URL` + `SUPABASE_SECRET_KEY`, clave solo del
servidor, nunca en git) y el de IAM (`IAM_SUPABASE_URL` + `IAM_SUPABASE_PUBLISHABLE_KEY`),
solo para validar a quién pertenece el token. No necesita la contraseña de PostgreSQL.

Pruebas del dominio: `out/build/x64-debug-vcpkg/garment-domain-tests.exe`.

## Worker de visualización (Python)

`visualization-worker/` pone 1 a 3 prendas del usuario sobre su foto con Cloudflare Workers AI
(FLUX.2 klein), en el puerto 8082. Como el `avatar-worker` de IAM, es un proceso aparte con
FastAPI y sin base de datos propia: obtiene las prendas del servicio del armario y la foto de
IAM con el token del usuario. Necesita `CF_ACCOUNT_ID` y `CF_API_TOKEN` en `.env`.

```powershell
cd visualization-worker
python -m venv .venv; .venv\Scripts\pip install -r requirements.txt
.venv\Scripts\python -m pytest      # pruebas con dobles, sin red
.\run.ps1                           # http://127.0.0.1:8082
```

Rutas: `POST /api/v1/outfit-visualizations` (202; `garment_ids`, `recommendation_id` opcional),
`GET /api/v1/outfit-visualizations/{id}` y `GET .../{id}/image` (PNG cuando está `READY`).
Los trabajos viven en memoria: se pierden al reiniciar el worker.

## Estado

Hecho: administración de prendas y los endpoints del contrato (`GET /garments`,
`POST /garments/resolve`, `GET /garments/{id}/image-access`).
Pendiente: escaneo de prendas con IA, límite de peticiones, Swagger, outfits guardados y
generación de imágenes.
