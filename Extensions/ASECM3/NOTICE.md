# Format metadata credits

RigidSimulation statically links the vendored Bullet 3.25 collision/dynamics/math
sources, upstream commit 2c204c49e56ed15ec5fcfa71d199ab6d6570b3f5. Bullet is by
Erwin Coumans and contributors, distributed under its preserved zlib license:
third_party/bullet/LICENSE.txt. Its source files remain unmodified; the local
root CMake wrapper and the M3 adapter are separate additions.

`schema/structures.xml` is copied from this workspace's `M3_to_M2/m3studio/structures.xml`. Its full original header credits are preserved. The schema identifies SC2Mapster/m3addon as the original source and credits Florian Koeberle, NiNtoxicated, Leruster, Witchsong, Teal, Blue Isle Studios, Volcore, Sixen, der_Ton, MrMoonKr, Skizot, Phygit, ufoZ, the SC2Mapster community, CaptainD001, Talv, TangorCraft, Solstice245 and Renee. See the XML header for full names and references.

`Schema.generated.inc` contains generated field metadata. The native implementation does not import a Python parser at runtime. Format interpretation is cross-referenced with the supplied SweetScape M3.bt revision 1.02.
