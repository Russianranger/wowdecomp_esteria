# Slime Giant native race install

Installed in G:/3.3.5a - Dev with the matched EsteriaWoW worldserver. Alliance race71 and Horde race72 use the user-corrected SlimeSkeleton3490792, not the rejected SlimeGiant2 behemoth.

Creator controls are exactly Slime Color (Green, Amber, Mauve, Red, Gold) and Slime Drip (Off/On). Existing skin/face bytes store the selections; other appearance bytes remain zero. One authored male body serves both factions.

The original 208-bone rig, 320 animation sequences, equipment sockets, source UVs and CASC originals are preserved. A separate skinned clothing collection uses transferred Kul Tiran HD UVs, the existing layout141 compositor, stock armor geosets and cloak texture2. Original helmet/shoulder/cloak sockets match Kul Tiran within floating-point tolerance.

Build and focused CTest passed. Native real-source validation passed for the engine skin profile and clothing bind rig; all55 streamed animation clips and all10 color/drip compositions passed. Server image, six mounted DBCs, SQL faction/class/start/language rows and existing character appearance preservation were verified. Native original-body preview rendered after a fresh launch. Full new-race creation, equipped rendering and relog remain live acceptance checks.

Receipt: G:/RetroportWork/slime-giant-175382/installation-receipt.json
Client rollback: G:/3.3.5a - Dev/_backups/slime-giant-20261020-113650
Server rollback: G:/EsteriaBackups/SlimeGiant-server-20261020-113616-837334
Tools: EsteriaWoW/tools/slime_giant_{source,clothing,race_pack,deploy,client_install}.py. Server rollback must retain any newly created Slime Giant characters; the rollback tool refuses to remove their support automatically.
