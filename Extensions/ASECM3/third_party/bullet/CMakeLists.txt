# Bullet 3.25, upstream commit 2c204c49e56ed15ec5fcfa71d199ab6d6570b3f5.
# Only the rigid solver, collision and math sources are vendored. No runtime
# downloads, Python, external DLL or modification to the WoW physics engine.
file(GLOB_RECURSE ASEC_M3_BULLET_SOURCES CONFIGURE_DEPENDS
    src/LinearMath/*.cpp src/BulletCollision/*.cpp src/BulletDynamics/*.cpp)
add_library(ASECM3Bullet STATIC ${ASEC_M3_BULLET_SOURCES})
target_include_directories(ASECM3Bullet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(ASECM3Bullet PUBLIC cxx_std_17)
target_compile_definitions(ASECM3Bullet PRIVATE NOMINMAX)
