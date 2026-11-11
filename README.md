# Digital Closet Service

API C++17 con Drogon para el armario digital de Mirage (prendas, imágenes privadas y los
tres endpoints que consume Recommendation). Supabase aporta PostgreSQL, Auth y Storage.

- Diseño, modelo y decisiones: [DIGITAL_CLOSET.md](DIGITAL_CLOSET.md)
- Contrato con Recommendation: `DIGITAL_CLOSET_RECOMMENDATION_CONTRACT.md` v1.0.1
- Migraciones: `supabase/migrations/` (aplicar en orden; `session_helper` solo si el
  proyecto no es el de IAM)

## Desarrollo local

Requisitos: Visual Studio con herramientas C++ x64, CMake, Ninja y Drogon con vcpkg
(`x64-windows`). Desde una consola de desarrollo x64:

```powershell
cmake --preset x64-debug -DCMAKE_TOOLCHAIN_FILE=C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build out/build/x64-debug-vcpkg
Copy-Item .env.example .env   # completar SUPABASE_URL y SUPABASE_PUBLISHABLE_KEY
```

El servidor escucha en `http://127.0.0.1:8081` (`/health`, `/api/v1/garments`). No necesita
la contraseña de PostgreSQL ni una `service_role` key: el JWT del usuario viaja a Supabase.

Pruebas del dominio: `out/build/x64-debug-vcpkg/garment-domain-tests.exe`.

## Estado

Hecho: administración de prendas y los endpoints del contrato (`GET /garments`,
`POST /garments/resolve`, `GET /garments/{id}/image-access`).
Pendiente: escaneo de prendas con IA, límite de peticiones, Swagger, outfits guardados y
generación de imágenes.
