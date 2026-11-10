#include "ERProceduralMap.h"
#include "ERMapTheme.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERMapThemeTest,"EclipseRecoil.Destruction.SharedMapThemes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERMapThemeTest::RunTest(const FString& Parameters)
{
    TSet<FName> SampleNames;TSet<const UERMapThemeLibrary*> Libraries;
    for(const TCHAR* Name:{TEXT("CentralBuilding"),TEXT("MiddleEasternAlleys"),TEXT("FireTraining"),TEXT("Rooms"),TEXT("Village"),TEXT("Town")})
    {
        auto* Saved=LoadObject<UERMapTemplate>(nullptr,*FString::Printf(TEXT("/Game/EclipseRecoil/Art/MapTemplates/DA_%s"),Name));
        if(!TestNotNull(TEXT("production recipe"),Saved) || !TestNotNull(TEXT("shared library bound"),Saved->ThemeLibrary.Get()))return false;
        Libraries.Add(Saved->ThemeLibrary);
        auto* Shared=Saved->ThemeLibrary->SharedSurfaces.Get();
        if(!TestTrue(TEXT("production templates require the reviewed shared catalog"),Saved->ThemeLibrary->RequireSharedSurfaces && Shared && Shared->IsValidLibrary()))return false;
        TestEqual(TEXT("reviewed catalog has 72 finishes"),Shared->Surfaces.Num(),72);
        for(const auto& Theme:Saved->ThemeLibrary->Themes)
        {
            TSet<UMaterialInterface*> Rows;for(const auto& M:Theme.Materials)Rows.Add(M.Get());
            TestEqual(TEXT("every physical material has its own recognizable finish"),Rows.Num(),Theme.Materials.Num());
        }
        auto* Raw=DuplicateObject<UERMapTemplate>(Saved,GetTransientPackage());Raw->ThemeLibrary=nullptr;Raw->RequireSharedSurfaces=false;
        FERProceduralConfig C;C.ExpectedTeamSize=3;FERBlockRoomConfig B;TSet<int32> TemplateSeen;
        for(int32 Seed=0;Seed<32;++Seed)
        {
            C.Seed=Seed;C.TemplateAsset=Raw;FERProceduralLayout Before,After,Again;
            if(!TestTrue(TEXT("original geometry builds"),ERProcedural::Build(C,B,Before)))return false;
            C.TemplateAsset=Saved;
            if(!TestTrue(TEXT("themed geometry builds"),ERProcedural::Build(C,B,After)))return false;
            TemplateSeen.Add(After.ThemeIndex);
            if(!SampleNames.Contains(After.ThemeName)){SampleNames.Add(After.ThemeName);AddInfo(FString::Printf(TEXT("ER_SHARED_THEME_SAMPLE theme=%s seed=%d"),*After.ThemeName.ToString(),Seed));}
            const auto& Palette=Saved->ThemeLibrary->Themes[After.ThemeIndex];
            TestTrue(TEXT("ground is from the shared catalog"),Shared->Contains(After.GroundSurface));
            for(const auto& Face:After.RoomShell)TestTrue(TEXT("room shell is from the shared catalog"),Shared->Contains(Face.Surface));
            if(Palette.ShellSteel)
            {
                TestTrue(TEXT("warehouse shell, court walls, frames, ladders and decks use distinct finishes"),
                    Palette.ShellSteel!=Palette.Wall && Palette.LadderSteel!=Palette.Materials[uint8(EERBlockMaterial::Metal)] &&
                    Palette.DeckSteel!=Palette.LadderSteel && Palette.DeckSteel!=Palette.Wall && Palette.DeckSteel!=Palette.ShellSteel);
            }
            if(Saved->Archetype==EERMapArchetype::FireTraining)
            {
                TSet<UMaterialInterface*> Walls,Cargo,Columns,LadderFinishes;
                for(const auto& D:After.Panels)
                    if(D.Material==EERBlockMaterial::Metal && D.Strength>1)Cargo.Add(D.Surface);
                    else if(D.Material==EERBlockMaterial::Concrete || D.Material==EERBlockMaterial::Wood)Walls.Add(D.Surface);
                for(const auto& D:After.FixedPieces)if(D.Mesh)
                {
                    const auto MeshName=D.Mesh->GetName();
                    if(MeshName.Contains(TEXT("Post")) || MeshName.Contains(TEXT("RoofArch")))Columns.Add(D.Surface);
                    if(MeshName.Contains(TEXT("SideWallBay")))Walls.Add(D.Surface);
                }
                LadderFinishes.Add(Palette.LadderSteel);
                for(const auto* Left:{&LadderFinishes,&Columns,&Cargo})for(auto* M:*Left)
                {
                    TestTrue(TEXT("firing range walls never share access, column or cargo finishes"),M && !Walls.Contains(M));
                    if(Left!=&Cargo)TestFalse(TEXT("cargo keeps a separate paint finish"),Cargo.Contains(M));
                    if(Left==&LadderFinishes)TestFalse(TEXT("ladders never share a column finish"),Columns.Contains(M));
                }
            }
            if(Saved->Archetype==EERMapArchetype::MiddleEasternAlleys)
            {
                TSet<EERBlockMaterial> HouseMaterials;
                for(const auto& D:After.Panels)if(D.SkinName.ToString().StartsWith(TEXT("AlleyModule_")))
                {
                    const FString Role=D.SkinName.ToString();
                    if(Role.EndsWith(TEXT("_Floor")) || Role.EndsWith(TEXT("_Roof")))
                    {
                        TestEqual(TEXT("house slabs use the floor finish"),D.Surface,Palette.Floor.Get());
                        const FString WallName=Role.LeftChop(Role.EndsWith(TEXT("_Roof")) ? 5:6);
                        const auto* Wall=After.Panels.FindByPredicate([&](const auto& V){return V.SkinName.ToString()==WallName;});
                        TestTrue(TEXT("house slab differs from its walls"),Wall && Wall->Surface!=D.Surface);
                    }
                    else
                    {
                        TestTrue(TEXT("alley houses are stone or brick"),D.Material==EERBlockMaterial::Stone || D.Material==EERBlockMaterial::Brick);
                        TestEqual(TEXT("house walls show their physical material"),D.Surface,Palette.Materials[uint8(D.Material)].Get());
                        HouseMaterials.Add(D.Material);
                    }
                }
                if(HouseMaterials.Num()>0 && After.Houses.Num()>=4)TestEqual(TEXT("both stone and brick houses appear"),HouseMaterials.Num(),2);
            }
            TestEqual(TEXT("panel count unchanged"),Before.Panels.Num(),After.Panels.Num());
            for(int32 I=0;I<Before.Panels.Num();++I)
            {
                const auto& A=Before.Panels[I];const auto& D=After.Panels[I];
                if(!TestTrue(TEXT("construction geometry, calibration, openings and support unchanged"),A.Id==D.Id && A.Material==D.Material && A.Strength==D.Strength && A.Transform.Equals(D.Transform) && A.Size==D.Size && A.Cell==D.Cell && A.Holes==D.Holes && A.bAnchorBottom==D.bAnchorBottom && A.bAnchored==D.bAnchored && A.bIndestructible==D.bIndestructible && A.SkinCRC==D.SkinCRC && A.Skin.Num()==D.Skin.Num()))return false;
                if(Palette.PreserveCargoPaint && D.Material==EERBlockMaterial::Metal && D.Strength>1)
                    TestEqual(TEXT("reinforced cargo keeps its authored paint"),D.Surface,A.Surface);
                else TestTrue(TEXT("finish follows physical material or ladder role"),D.SkinName.ToString().StartsWith(TEXT("AlleyModule_")) || D.Surface==Palette.Materials[uint8(D.Material)] ||
                    (Palette.LadderSteel && D.Material==EERBlockMaterial::Metal && D.Surface==Palette.LadderSteel) ||
                    (Palette.CargoSteel && D.Material==EERBlockMaterial::Metal && D.Strength>1 && D.Surface==Palette.CargoSteel));
                TestTrue(TEXT("panel has no legacy surface fallback"),Shared->Contains(D.Surface));
                if(!D.Skin.IsEmpty())TestTrue(TEXT("skin interior is from the shared catalog"),Shared->Contains(D.InteriorSurface));
            }
            TestEqual(TEXT("fixed piece count unchanged"),Before.FixedPieces.Num(),After.FixedPieces.Num());
            for(int32 I=0;I<Before.FixedPieces.Num();++I)
            {
                const auto& A=Before.FixedPieces[I];const auto& D=After.FixedPieces[I];
                const bool Scenery=Saved->Archetype==EERMapArchetype::Village && D.VisualOnly && D.Mesh &&
                    (D.Mesh==Saved->Village.SkyDomeMesh || Saved->Village.ExteriorTrees.Contains(D.Mesh));
                const bool Lamp=D.Surface && D.Surface==Saved->Rooms.LampSurface;
                if(!D.CollisionOnly && !Scenery && !Lamp)TestTrue(TEXT("fixed structure has no material-slot fallback"),Shared->Contains(D.Surface));
                TestTrue(TEXT("original mesh, dimensions and collision retained"),A.Transform.Equals(D.Transform) && A.Mesh==D.Mesh && A.Material==D.Material && A.Roof==D.Roof && A.VisualOnly==D.VisualOnly && A.MeshCollision==D.MeshCollision && A.CollisionOnly==D.CollisionOnly);
                if(Saved->Archetype==EERMapArchetype::Village && D.VisualOnly && !D.Mesh)TestEqual(TEXT("Village exterior ground matches selected terrain"),D.Surface,After.GroundSurface);
                if(Palette.LadderSteel && D.PawnBarrier && !D.CollisionOnly)TestEqual(TEXT("parapets use the ladder/detail finish"),D.Surface,Palette.LadderSteel.Get());
                if(Palette.ShellSteel && D.Mesh && D.Mesh->GetName().Contains(TEXT("SideWallBay")))TestEqual(TEXT("warehouse wall sheets retain a dedicated finish"),D.Surface,Palette.ShellSteel.Get());
            }
            if(Palette.LadderSteel && Saved->Archetype!=EERMapArchetype::Village)for(const auto& Ladder:After.Ladders)
            {
                const FVector Rung=Ladder.Frame.TransformPosition({0,0,25});
                const auto* Panel=After.Panels.FindByPredicate([&](const auto& D){return D.Material==EERBlockMaterial::Metal && FVector::DistSquared(D.Transform.TransformPosition(D.Size*.5),Rung)<9;});
                // Fixed instanced ladders carry their rungs as fixed pieces.
                const auto* Piece=After.FixedPieces.FindByPredicate([&](const auto& D){return D.Material==EERBlockMaterial::Metal && FVector::DistSquared(D.Transform.GetLocation(),Rung)<9;});
                if(TestTrue(TEXT("physical ladder has a first rung"),Panel || Piece))TestEqual(TEXT("ladder rungs get the light galvanized finish"),Panel ? Panel->Surface:Piece->Surface,Palette.LadderSteel.Get());
            }
            TestTrue(TEXT("same arena and starts"),Before.ArenaSize==After.ArenaSize && Before.AlphaSpawns.Num()==After.AlphaSpawns.Num() && Before.OmegaSpawns.Num()==After.OmegaSpawns.Num());
            Again=Before;TestTrue(TEXT("cosmetic pass deterministic"),ERProcedural::ApplyMapTheme(C,Again));
            TestTrue(TEXT("repeat theme and ground"),Again.ThemeIndex==After.ThemeIndex && Again.GroundSurface==After.GroundSurface);
            for(int32 I=0;I<After.Panels.Num();++I)TestEqual(TEXT("role finishes deterministic"),Again.Panels[I].Surface,After.Panels[I].Surface);
        }
        TestEqual(TEXT("every theme sampled on this template"),TemplateSeen.Num(),Saved->ThemeLibrary->Themes.Num());
        auto* Invalid=DuplicateObject<UERMapTemplate>(Saved,GetTransientPackage());Invalid->ThemeLibrary=DuplicateObject<UERMapThemeLibrary>(Saved->ThemeLibrary,GetTransientPackage());
        Invalid->ThemeLibrary->Themes.Last().Materials.Reset();C.TemplateAsset=Invalid;
        FERProceduralLayout Prior;Prior.ThemeName=TEXT("unchanged");
        TestFalse(TEXT("incomplete theme library rejected"),ERProcedural::Build(C,B,Prior));
        TestEqual(TEXT("rejection leaves output intact"),Prior.ThemeName,FName(TEXT("unchanged")));
        Invalid->ThemeLibrary=DuplicateObject<UERMapThemeLibrary>(Saved->ThemeLibrary,GetTransientPackage());
        if(Invalid->ThemeLibrary->Themes.Last().ShellSteel)
        {Invalid->ThemeLibrary->Themes.Last().DeckSteel=nullptr;TestFalse(TEXT("partial steel role palette rejected"),ERProcedural::Build(C,B,Prior));}
        Invalid->ThemeLibrary=DuplicateObject<UERMapThemeLibrary>(Saved->ThemeLibrary,GetTransientPackage());
        Invalid->ThemeLibrary->SharedSurfaces=nullptr;
        TestFalse(TEXT("missing required shared catalog rejected"),ERProcedural::Build(C,B,Prior));
        Invalid->ThemeLibrary=DuplicateObject<UERMapThemeLibrary>(Saved->ThemeLibrary,GetTransientPackage());
        Invalid->ThemeLibrary->Themes.Last().Wall=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/EclipseRecoil/Art/ProceduralTextures/M_ER_PBR_Plaster001"));
        TestFalse(TEXT("finish outside catalog rejected atomically"),ERProcedural::Build(C,B,Prior));
        TestEqual(TEXT("invalid catalog preserves caller output"),Prior.ThemeName,FName(TEXT("unchanged")));
    }
    TestEqual(TEXT("every template owns its palette library"),Libraries.Num(),6);
    AddInfo(TEXT("ER_MAP_PALETTES templates=6 seeds=32 geometry=unchanged deterministic=PASS"));return true;
}
#endif
