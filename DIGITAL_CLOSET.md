# Digital Closet service: diseño

Estado: propuesta para revisar antes de implementar. Sigue las convenciones de
`iam-service` (C++17, Drogon, DDD/CQRS, Supabase) y el contrato
`DIGITAL_CLOSET_RECOMMENDATION_CONTRACT.md` v1.0.1.

## 1. Decisiones de arquitectura

| Decisión | Elección | Motivo |
| --- | --- | --- |
| Ubicación | Proyecto propio `digital-closet-service/`, hermano de `iam-service/`, puerto 8081 | IAM ya tiene su propio repositorio y ciclo; el contrato permite un bounded context separado. |
| Stack | C++17 + Drogon + Supabase (PostgreSQL, Storage), igual que IAM | Pedido explícito: seguir cómo está hecho IAM. |
| Identidad | El JWT del usuario viaja a PostgREST; el propietario sale de `auth.uid()` en SQL | Es el patrón de IAM (RPC `SECURITY INVOKER` + RLS). No hay `service_role` ni `user_id` del cliente. |
| Sesión revocada | Las políticas RLS reutilizan `public.mirage_session_active()` | Logout inmediato, igual que `user_profiles`. |
| Código compartido | Se copian `shared/` (`ApiResult`, `SupabaseClient`, `HttpSupport`) | shortcut: duplicación de 3 archivos pequeños; extraer a una librería común si aparece un tercer servicio. |
| Imágenes | Bucket privado `garment-images`; Flutter sube directo a Storage con su JWT; el servicio solo registra la referencia y firma URLs | El servicio no proxea bytes y no necesita claves privilegiadas. |

## 2. Organización (misma forma que `profile/`)

```text
digital-closet-service/
  garment/
    domain/model/        Garment (agregado), GarmentImage, GarmentCategory, GarmentAvailability
    domain/repositories/ GarmentRepository (puerto), GarmentAccess {bearer}
    application/         GarmentCommands.h, GarmentServices (Command/Query), ResolveService
    infrastructure/      SupabaseGarmentRepository (RPC), GarmentJsonMapper, SupabaseImageSigner
    interfaces/          GarmentController
  outfit/                (fase 2) Outfit, OutfitVisualization, ImageGenerator (puerto)
  shared/                copiado de iam-service
  supabase/migrations/   garments, outfits, bucket y políticas
  docs/openapi.json      Swagger local como IAM
  tests/                 garment_domain_tests.cpp, garment_smoke.py (mock), openapi_contract.py
```

Escritura: Controller → comando → CommandService → agregado → repositorio → RPC.
Lectura: Controller → consulta → QueryService → repositorio. El dominio compila sin
Drogon ni JsonCpp.

## 3. Modelo de dominio

**Garment** (agregado raíz): `id` (UUID), `name` (1–120), `category` (enum cerrado:
TOP, BOTTOM, ONE_PIECE, OUTERWEAR, FOOTWEAR, ACCESSORY), `subcategory` (opcional,
1–60, MAYÚSCULAS_CON_GUION_BAJO), `colors` y `styles` (listas únicas de códigos del
vocabulario del contrato), `description` (1–1000), `availability`
(AVAILABLE/UNAVAILABLE), `image` (opcional), `revision`, `updatedAt`.
El propietario no es campo del dominio: lo impone SQL.

**GarmentImage** (value object): `id`, `version`.

Reglas: validar antes de mutar (sin cambios parciales), listas sin duplicados, `name`
no solo espacios, categoría y descripción obligatorias (mínimo para recomendar).
`revision` sube en cualquier cambio observable (incluida imagen y disponibilidad) y
nunca retrocede; `image.version` sube al reemplazar el contenido de la imagen.

**Fase 2:** `Outfit` (agregado: `id`, `name`, ítems = IDs de prendas propias,
`recommendation_id` opcional) y `OutfitVisualization` (agregado: estado
PENDING → PROCESSING → READY | FAILED, referencia a la imagen resultante). El
puerto `ImageGenerator` aísla al proveedor de imágenes.

## 4. Persistencia (PostgreSQL / Supabase)

Sigue el recuadro **Digital Closet (Wardrobe Database)** del diagrama de base de datos del
proyecto. Migración aditiva en una transacción, como IAM.

- Tablas del diagrama: `wardrobes` (una por usuario, se crea con la primera prenda),
  `garment_categories` (las 6 del contrato), `garment_types` (los códigos abiertos de
  `subcategory`), `garments`, `garment_secondary_colors` y `store_garment_references`
  (creada, sin uso hasta que exista el servicio de tienda).
- Correspondencia con el contrato: `category` = categoría del tipo; `subcategory` = nombre
  del tipo, o `null` si el tipo se llama como su categoría; `colors` = `primary_color`
  primero y los secundarios ordenados después.
- **Desviaciones del diagrama**, a reflejar en el informe: `primary_color` admite null
  (el contrato permite colores desconocidos); `name` hasta 120 y tipos hasta 60 caracteres
  (contrato); `image_url` guarda la ruta del objeto en el bucket privado, no una URL
  pública; y `garments` añade columnas que el contrato exige: `styles`, `description`,
  `availability`, `image_id`, `image_version`, `revision` y `deleted_at` (borrado lógico).
- `revision` la incrementa un trigger en cada `UPDATE` de `garments`; las RPC omiten la
  escritura si nada cambió. Un cambio solo en colores secundarios también la incrementa.
- RLS en todas las tablas por propietario del armario y sesión activa. Las categorías y
  los tipos son de lectura para cualquier usuario autenticado.
- RPC `SECURITY INVOKER`, `search_path` vacío: `mirage_create_garment`,
  `mirage_update_garment`, `mirage_delete_garment`, `mirage_get_garment`,
  `mirage_list_garments`, `mirage_resolve_garments`, `mirage_set_garment_image`,
  `mirage_garment_image`.
- shortcut: cualquier usuario autenticado puede crear un código de subcategoría nuevo (la
  tabla de tipos es global, como en el diagrama); curar la lista si se abusa.
- Storage: bucket privado `garment-images` y políticas en `storage.objects` solo para la
  carpeta `{auth.uid()}/...`. Ruta del objeto: `{user_id}/{garment_id}/{image_id}`.
- Fase 2: `outfits`, `outfit_items`, `outfit_visualizations` y bucket
  `outfit-visualizations`, con las mismas políticas.

## 5. Endpoints

Base `/api/v1`, `Authorization: Bearer`. Los tres primeros son el contrato con
Recommendation y no cambian de forma (JSON `snake_case` según el contrato).

| Método y ruta | Operación |
| --- | --- |
| `GET /garments?limit&cursor` | Listar (contrato §5) |
| `POST /garments/resolve` | Resolver por IDs (contrato §6) |
| `GET /garments/{id}/image-access` | URL firmada de 5 min (contrato §7) |
| `POST /garments` | Registrar prenda (201) |
| `GET /garments/{id}` | Leer una prenda |
| `PUT /garments/{id}` | Reemplazar atributos y disponibilidad (como `PUT` de Profile) |
| `DELETE /garments/{id}` | Borrado lógico (204) |
| `PUT /garments/{id}/image` | Registrar imagen ya subida a Storage: sube `image.version` |
| `GET /health`, `/docs`, `/openapi.json` | Igual que IAM |

Fase 2: `POST/GET /outfits`, `GET/DELETE /outfits/{id}`,
`POST /outfit-visualizations` (202), `GET /outfit-visualizations/{id}`.

## 6. Cambios propuestos al contrato (coordinar con Recommendation)

1. **Sobre de errores (§8): sin cambios.** Se implementa tal cual el contrato,
   `{"error":{"code":"GARMENT_NOT_FOUND","message":"..."}}`, aunque IAM usa
   `{"error":"codigo"}`. El contrato pide acordar un cambio *antes* de implementar y
   aún no se ha acordado. Si ambos desarrolladores prefieren el estándar de IAM, es un
   cambio local en `shared/interfaces/HttpSupport.cpp`.
2. **Campos en `snake_case`** del contrato frente al `camelCase` de IAM: se mantiene el
   contrato, porque ya está acordado entre ambos desarrolladores.
3. **Nuevo endpoint opcional** `POST /garments/analysis` (escaneo de una foto con visión
   para sugerir atributos). No altera ningún endpoint existente. Pendiente.
4. **Rutas de administración añadidas** que el contrato no cubre, sin alterar sus
   endpoints: `POST /garments`, `GET|PUT|DELETE /garments/{id}` y
   `PUT /garments/{id}/image`. Recommendation no las usa.

## Estado de la implementación

| Pieza | Estado |
| --- | --- |
| Dominio `Garment`, comandos/consultas, servicio de imagen | Hecho; compila y pasa `garment-domain-tests` |
| Migración `20261110120000_garments.sql` (esquema del diagrama) | Aplicada y probada en el proyecto de desarrollo `mirage-digital-closet`; avisos de seguridad vacíos |
| Repositorio Supabase, mapeador, firmador de imágenes, controlador | Compilan; 8 rutas probadas por HTTP contra Supabase real |
| Rate limit (`429` + `Retry-After`) | No implementado |
| Swagger / OpenAPI | Pendiente |
| Outfits guardados y visualización (fase 2) | Pendiente |

## 7. Escaneo de prendas y generación de imágenes

- **Escaneo:** modelo de visión (candidatos: Nemotron omni de NVIDIA, Gemini Flash)
  detrás de un puerto `GarmentAnalyzer`. Solo sugiere; el usuario confirma y sus
  correcciones tienen prioridad (contrato §9). No se infiere material ni talla.
- **Visualización:** puerto `ImageGenerator`. Adaptador probado: Cloudflare Workers AI
  con FLUX.2 klein (hasta 4 imágenes de entrada). Alternativa de respaldo: la Space
  Qwen-Image-2.1. NVIDIA no sirve: su servicio gratuito rechaza fotos propias.
- La clave del proveedor vive en variables de entorno del servidor, nunca en el
  código ni en el repositorio.

## 8. Verificación prevista

- CTest `garment-domain`: reglas, enums, listas duplicadas, conservación de estado ante
  errores, `revision` e imagen.
- `tests/garment_smoke.py` con un mock de Supabase: contratos HTTP, IDOR, resolución
  mixta, paginación y errores.
- `tests/openapi_contract.py`: el contrato OpenAPI coincide con las rutas.
- Pruebas SQL y de RLS contra una base PostgreSQL local (solo en una máquina con
  PostgreSQL, como `profile_sql.py`).

## 9. Decisiones pendientes

1. Aprobar los cambios del §6 con quien desarrolla Recommendation.
2. Recurso de la persona para la visualización (`person_reference_id`): ¿la foto de
   perfil de Profile (`profileImageUrl`) o una foto dedicada? Requiere acuerdo con Profile.
3. Consentimiento de privacidad para enviar la foto de la persona al proveedor de imágenes.
4. Alojamiento: la cuenta gratuita de Cloudflare es suficiente para una demo; para un
   servicio estable hay que valorar costos.
