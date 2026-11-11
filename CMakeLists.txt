cmake_minimum_required (VERSION 3.10)

# vcpkg location on this machine (must come before project)
set(CMAKE_TOOLCHAIN_FILE "C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

project ("digital-closet-service")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Drogon CONFIG REQUIRED)

add_executable(digital-closet-service
    "digital-closet-service.cpp"
    "shared/interfaces/HttpSupport.cpp"
    "shared/infrastructure/SupabaseClient.cpp"
    "garment/domain/model/Garment.cpp"
    "garment/application/GarmentServices.cpp"
    "garment/infrastructure/GarmentJsonMapper.cpp"
    "garment/infrastructure/SupabaseGarmentRepository.cpp"
    "garment/infrastructure/SupabaseImageSigner.cpp"
    "garment/infrastructure/SupabaseIdentityProvider.cpp"
    "garment/interfaces/GarmentController.cpp"
    "outfit/interfaces/OutfitController.cpp"
    "shared/interfaces/ApiDocumentation.cpp"
)
target_link_libraries(digital-closet-service PRIVATE Drogon::Drogon)

# Ninja presets place the executable here; reconfigure when documentation changes.
foreach(asset index.html openapi.json swagger-ui.css swagger-ui-bundle.js swagger-initializer.js LICENSE VENDOR.md)
    configure_file("docs/${asset}" "${CMAKE_CURRENT_BINARY_DIR}/docs/${asset}" COPYONLY)
endforeach()

include(CTest)
if(BUILD_TESTING)
    # The domain and application layers compile without Drogon or JsonCpp.
    add_executable(garment-domain-tests tests/garment_domain_tests.cpp
        garment/domain/model/Garment.cpp garment/application/GarmentServices.cpp)
    target_compile_features(garment-domain-tests PRIVATE cxx_std_17)
    add_test(NAME garment-domain COMMAND garment-domain-tests)
endif()
