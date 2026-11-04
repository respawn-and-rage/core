# Characters

**Code:** `ERTeamAppearance.h`, `ERPostureMesh`, `ERCorpse`, `ERMenuPreview`, `scripts/character_catalog.py`.

## Catalog

20 selectable skins, 10 per team (`ERSkins::Count`), with stable indices. Labels come from
`scripts/character_catalog.py`; saved legacy IDs resolve as aliases.

| Index | Alpha (blue) | Omega (orange) |
| ---: | --- | --- |
| 0 | Wraith · Officer | Drongo · Raider |
| 1 | Wraith · Assault | Drongo · Jackal |
| 2 | Wraith · Patrol | Drongo · Assault |
| 3 | Twinblast · Recon | Drongo · Smuggler |
| 4 | Twinblast · Support | Drongo · Militia |
| 5 | Belica · Infantry | Gadget · Recon |
| 6 | Belica · Arctic Patrol | Gadget · Mechanic |
| 7 | Belica · NBC Defense | Gadget · Patrol |
| 8 | Belica · Armored | Gadget · Field Engineer |
| 9 | Belica · Support | Gadget · Veteran |

Note that the gallery counts from 1: "gallery 8" is index 7. Role assignments are in
[EQUIPMENT.md](EQUIPMENT.md). Appearances live in three catalogs that must agree:
`BP_ERCharacter`, `BP_ERTrainingTarget`, `BP_ERCorpse`. Each team has a canonical fallback
(Alpha: Officer; Omega: Raider). Corpses keep the skin worn at death.

## Visual rules

- Matte blue/neutral Alpha and muted orange/neutral Omega, with natural skin and hair, neutral
  lower gear, no hero emissive. Belica and Drongo use owned muted materials (Belica blue
  `(.12,.28,.52)`, Drongo ochre about `(.62,.29,.115)`, desaturation .85, white lift .08/.10) marked
  `ERMutedLegacyPalette=1`, so `setup_team_readability.py` skips them. Do not brighten.
- Hide original weapons and tech props through material sections or hidden bones, without
  hiding body or head bones. No backpack attachments on selectable skins.
- Wraith 0/1/2 are the OD Green, base and Lunar Ops costumes. Officer has Twinblast Officer's
  sunglasses head on the live head bone (`SK_ER_WraithOfficer_Unmasked`); Assault has
  Twinblast Support's head; Patrol stays masked. `restore_wraith_assault_mask.py` undoes Assault.
- Drongo Jackal keeps Scavenger's goggles, skull bandana and pad (material slot 0, misleadingly
  `M00_Teddybear`), through owned `M_ER_Scavenger_FieldMask`; marker `ERScavengerOriginalHead`
  protects it from `setup_drongo_masks.py`. The atlas R channel is white BaseColor paint.
- Gadget · Patrol is the Sniper look. Veteran's Shellshock is an optional catalog choice;
  Gadget's authored skin masks must survive tinting on both body sections.
- Gadget Recon/Mechanic/Patrol share an owned shadow PhysicsAsset to avoid the vendor Rig.

## Rig and animation

- FPS: `Characters/FirstPerson/SKEL_ER_FirstPerson`, independent of world skins. Skin FPS
  meshes keep only forearm and hand geometry. Never move or scale a hero body to frame arms.
  `setup_skin_first_person_forearms.py` builds them; `-ERFPSForearmsGallery` captures them.
- Cooked playback: see the FPS clips note in [PACKAGE_MAC.md](PACKAGE_MAC.md).
- Ragdolls: owned `PA_ER_{Alpha,Omega}_Ragdoll`, 16 bodies/15 joints. Corpse meshes collapse
  hidden bones in `ERCorpseMeshComponent::FinalizeBoneTransform`.
- Prone is procedural on team rigs through `UERPostureMesh`. Its extra hit volume stops at death.
- Prone keeps the torso low, opens the thighs from the hips, and fits the arms
  forward. Ground traces set the leg height and select reachable elbow support
  points. The boot toes turn down across the shins for ground contact. Side probes
  reduce thigh spread near obstacles, and elbows lift when a wall prevents
  ground support.
  Mesh-specific clothing/body clearances live in the `RespawnAndRage.ProneGrounding`
  section of `DefaultGame.ini`. `ProneContactsLeft` and `ProneContactsRight` store
  mesh-specific toe, knee and elbow offsets in centimetres (X, Y, Z).
  `PronePelvisDepth` limits hip drop for meshes with low clothing.
  `calibrate-prone-grounding.py --apply` measures the
  visible torso/leg vertices of all 20 skins and updates only that section; anchored
  hand and foot contacts are excluded from root-height calibration. Recheck
  with `test-world-poses.py --prone --wall` and `test-hit-fidelity.py` afterwards.
- Ground contact: `FeetOffset = -92.5 - soleHeight`, measured from foot vertices across the
  idle clip by `setup_character_grounding.py`. Rerun after an explicit catalog rebuild.
  `UERMovement::ApplyPosturePresentation` lowers live pawns a further 3cm: the capsule walks 2.15cm
  above the floor and the live idle stands about .85cm taller than the clip. Standing
  and crouching have no terrain IK; prone has foot and elbow ground contact.

## Clothing

Owned cloth meshes use embedded `ER_FieldClothCollision`, reusing Twinblast's authored
collision and separate from gameplay physics: 24cm travel, 3cm backstops, CCD, 1.2cm thickness,
4 iterations / 2 substeps. All render LODs keep their own mapping. In UE 5.8 bind with
`FSkeletalMeshClothingSystemUtilities::AssignClothingToSection`; `BindToSkeletalMesh` alone loses it.
`setup_field_clothing.py -ERClothOnly` saves only owned meshes.

`AERMenuPreview` needs `bWaitForParallelClothTask` before its manual `CaptureScene`; test
cloth through that real preview. It keeps `bForceMipStreaming` for capture detail, scoped to
that component. `test-field-clothing.py` proves simulation; screenshots alone cannot.

## Rebuild

Approved skins are saved and ready. Source packs were pruned to the runtime closure, so
historical import scripts need archived inputs and are not launch steps. Original art is
immutable. Validate the state with `scripts/verify_runtime_characters.py` against
`docs/CHARACTER_RUNTIME_STATE.json`, which lists hashes and deleted packages.
`setup_*`/`verify_*` pairs exist per skin family. Keep Paragon geometry limits in mind:
do not claim cybernetic limbs became human.

## Verify

`test-team-skins.py`, `test-target-skins.py`, `test-world-poses.py`, `test-field-clothing.py --render --preview`,
`test-character-preview.py`, `verify_runtime_characters.py`, `test-network.py --skins`,
`test-hit-fidelity.py` (live sole height against the floor).
`test-world-poses.py --prone --wall` checks visible mesh floor clearance, elbows,
weapon bounds and both palm contacts across all skins on server and client.
