#include "ERProceduralMap.h"
#include "ERLadder.h"
#include "ERMapTemplate.h"
#include "ERMapTheme.h"
#include "ERIndustrialFence.h"
#include "ERMapPlayability.h"
#include "ERMapSelection.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif
#include "ERBotController.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

namespace ERProcedural { bool BuildGeometry(const FERProceduralConfig&,const FERBlockRoomConfig&,FERProceduralLayout&,const FERCellSettings&); bool BuildTemplate(const FERProceduralConfig&,const FERBlockRoomConfig&,FERProceduralLayout&,const FERCellSettings&); }
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"

static bool BuildLevel(const FERProceduralConfig& C, const FERBlockRoomConfig& B, FERProceduralLayout& Out,bool StairEntrance,bool ReturnEntrance,const FERCellSettings& Cells)
{
    // Bounded, aligned MVP. Reject unsupported dimensions before allocating.
    const FVector S = B.BlockSize;
    if (S != FVector(50,20,25) || !FMath::IsFinite(B.Height) || B.Height < 250 || B.Height > 500 ||
        FMath::Fmod(B.Height,25) != 0 || !FMath::IsFinite(B.ChunkSize) || B.ChunkSize < 100 ||
        C.RoomCount < 6 || C.RoomCount > 10 || C.MinRoomSize < 400 || C.MaxRoomSize > 800 || C.MinRoomSize > C.MaxRoomSize ||
        C.MinRoomSize % 50 || C.MaxRoomSize % 50 || C.CorridorWidth < 150 || C.CorridorWidth > 400 || C.CorridorWidth % 50 ||
        C.BranchLength < 150 || C.BranchLength > 800 || C.BranchLength % 50 || C.DoorWidth < 100 || C.DoorWidth > 200 || C.DoorWidth % 50 ||
        C.DoorHeight < 200 || C.DoorHeight > B.Height - 25 || C.DoorHeight % 25) return false;
    FERProceduralLayout L;
    FRandomStream Random(C.Seed);
    const int32 Columns = (C.RoomCount + 1) / 2;
    const float HalfHall = C.CorridorWidth * .5f;
    const float Near = HalfHall + C.BranchLength;
    L.Corridors.Add({FVector2D(-450,-HalfHall), FVector2D((Columns-1)*1000+450,HalfHall)});
    auto Wall = [&](FVector Start, FVector Direction, float Length, EERBlockMaterial Material)
    {
        return L.Walls.Add({Start, Direction, Length, Material});
    };
    auto Rectangle = [&](const FERLayoutRect& R, EERBlockMaterial M)
    {
        const int32 First = L.Walls.Num();
        // Horizontal sides stop at the inside corners. Vertical returns own them.
        Wall(FVector(R.Min.X,R.Min.Y-10,0), FVector(1,0,0), R.Max.X-R.Min.X, M);
        Wall(FVector(R.Max.X+10,R.Min.Y-20,0), FVector(0,1,0), R.Max.Y-R.Min.Y+40, M);
        Wall(FVector(R.Min.X,R.Max.Y+10,0), FVector(1,0,0), R.Max.X-R.Min.X, M);
        Wall(FVector(R.Min.X-10,R.Min.Y-20,0), FVector(0,1,0), R.Max.Y-R.Min.Y+40, M);
        return First;
    };
    const int32 Hall = Rectangle(L.Corridors[0], EERBlockMaterial::Concrete);
    if(StairEntrance)L.Openings.Add({Hall+3,float(20+(C.CorridorWidth-150)*.5),150,0,B.Height,false});
    if(ReturnEntrance)L.Openings.Add({Hall+2,0,100,0,float(C.DoorHeight),false});
    for (int32 Index = 0; Index < C.RoomCount; ++Index)
    {
        const int32 Col = Index / 2;
        const bool Top = Index % 2 == 0;
        const float X = Col * 1000;
        const float W = Random.RandRange(C.MinRoomSize/50,C.MaxRoomSize/50)*50;
        const float H = Random.RandRange(C.MinRoomSize/50,C.MaxRoomSize/50)*50;
        FERLayoutRect R{FVector2D(X-W*.5f, Top ? Near : -Near-H), FVector2D(X+W*.5f, Top ? Near+H : -Near), Index};
        L.Rooms.Add(R);
        const int32 Choice=(Index+Random.RandRange(0,2))%3;
        const auto Material=static_cast<EERBlockMaterial>(C.AdditionalMaterials ? (Index+uint32(C.Seed)%6)%6 : Choice);
        const int32 First = Rectangle(R, Material);
        const int32 DoorWall = First + (Top ? 0 : 2);
        L.Openings.Add({DoorWall, (W-C.DoorWidth)*.5f, float(C.DoorWidth), 0, float(C.DoorHeight), false});
        // Common hall door coordinates are aligned from its fixed -450cm origin.
        // Width=150 requires an offset25. Split intervals below clip exact jambs.
        L.Openings.Add({Hall+(Top ? 2 : 0), X+450-C.DoorWidth*.5f, float(C.DoorWidth), 0, float(C.DoorHeight), false});
        const int32 WindowColumn = Random.RandRange(1, int32(W/50)-3);
        L.Openings.Add({First+(Top ? 2 : 0), float(WindowColumn*50), 100, 100, 100, true});
        const float Y0 = Top ? HalfHall : -Near, Y1 = Top ? Near : -HalfHall;
        L.Corridors.Add({FVector2D(X-C.DoorWidth*.5f,Y0), FVector2D(X+C.DoorWidth*.5f,Y1)});
        // No end walls: the existing room/hall walls own each doorway. Trim
        // branch sides to touch their outer faces, avoiding corner duplication.
        for (float Side : {-1.f,1.f})
            Wall(FVector(X+Side*(C.DoorWidth*.5f+10),Y0+20,0), FVector(0,1,0), Y1-Y0-40, Material);
        const FVector Center((R.Min.X+R.Max.X)*.5f,(R.Min.Y+R.Max.Y)*.5f,100);
        L.Routes.Add({Center,FVector(X,Top ? Near-50 : -Near+50,100),FVector(X,0,100)});
        if (Index == 0) L.Spawn = Center;
    }
    // Each wall slot becomes its construction's cell panels; openings are holes
    // (door jambs stay exact: openings lie on the 25cm grid, cells on 5cm).
    const float T=B.BlockSize.Y;
    for (int32 W=0; W<L.Walls.Num(); ++W)
    {
        const auto& D=L.Walls[W];
        const FRotator R=D.Direction.Rotation();
        const FVector Across=R.RotateVector(FVector(0,1,0));
        const int32 First=L.Panels.Num();
        FERCellWorld::AddConstruction(W,D.Material,FTransform(R,D.Start-Across*T*.5f),FVector(D.Length,T,B.Height),Cells,L.Panels);
        for (int32 P=First; P<L.Panels.Num(); ++P)
        {
            L.Panels[P].bAnchorBottom=true;
            for (const auto& O:L.Openings) if (O.Wall==W) L.Panels[P].Holes.Add(FBox2D(FVector2D(O.Along,O.Sill-D.Start.Z),FVector2D(O.Along+O.Width,O.Sill-D.Start.Z+O.Height)));
        }
    }
    if (C.WindowGlass)
        for (const auto& O:L.Openings) if (O.bWindow)
        {
            // One pane per window, resting on its fixed frame.
            const auto& D=L.Walls[O.Wall];
            const FRotator R=D.Direction.Rotation();
            const FVector Across=R.RotateVector(FVector(0,1,0));
            const int32 First=L.Panels.Num();
            FERCellWorld::AddConstruction(100000+O.Wall,EERBlockMaterial::Glass,FTransform(R,D.Start-Across*T*.5f+D.Direction*O.Along+FVector(0,0,O.Sill)),FVector(O.Width,T,O.Height),Cells,L.Panels);
            for (int32 P=First; P<L.Panels.Num(); ++P) L.Panels[P].bAnchorBottom=true;
        }
    Out=MoveTemp(L); return true;
}
bool ERProcedural::BuildGeometry(const FERProceduralConfig& C,const FERBlockRoomConfig& B,FERProceduralLayout& Out,const FERCellSettings& Cells)
{
    if(C.TemplateAsset && C.TemplateAsset->Symmetry==EERTemplateSymmetry::Mirror &&
       C.TemplateAsset->Archetype!=EERMapArchetype::LegacyAssembly &&
       (C.TemplateAsset->Archetype!=EERMapArchetype::CentralBuilding || !C.TemplateAsset->CentralBuilding.CircularTerrain))
    {
        TStrongObjectPtr<UERMapTemplate> Canonical(DuplicateObject<UERMapTemplate>(C.TemplateAsset,GetTransientPackage()));
        Canonical->Symmetry=Canonical->Archetype==EERMapArchetype::MiddleEasternAlleys ? EERTemplateSymmetry::None:EERTemplateSymmetry::HalfTurn;
        Canonical->ReflectionBuild=true;
        Canonical->CompositionSeed=C.TemplateAsset->CompositionSeed;Canonical->SizingTeamSize=C.TemplateAsset->SizingTeamSize;Canonical->SizingRoomsGroundOnly=C.TemplateAsset->SizingRoomsGroundOnly;Canonical->SizingArena=C.TemplateAsset->SizingArena;
        FERProceduralConfig Paired=C;Paired.TemplateAsset=Canonical.Get();FERProceduralLayout Next;
        if(!BuildGeometry(Paired,B,Next,Cells) || !FoldLayoutToMirror(Next,Cells))return false;
        Out=MoveTemp(Next);return true;
    }
    if(C.TemplateAsset && C.TemplateAsset->Archetype==EERMapArchetype::Village) return BuildVillage(C,B,Out,Cells);
    if(C.TemplateAsset && C.TemplateAsset->Archetype==EERMapArchetype::Rooms) return BuildRooms(C,B,Out,Cells);
    if(C.TemplateAsset && C.TemplateAsset->Archetype==EERMapArchetype::FireTraining)
    {
        // Bounded deterministic retries; the final attempt is the verified
        // reference layout, so an accepted seed never leaves the arena empty.
        for(int32 Attempt=0;Attempt<ERProcedural::FireTrainingAttempts;++Attempt)
        {
            FERProceduralConfig Try=C;Try.LayoutAttempt=Attempt;
            if(BuildTemplate(Try,B,Out,Cells))return true;
            UE_LOG(LogTemp,Verbose,TEXT("ER_FIRE_TRAINING_RETRY seed=%d attempt=%d"),C.Seed,Attempt);
        }
        UE_LOG(LogTemp,Warning,TEXT("ER_FIRE_TRAINING_REJECT seed=%d"),C.Seed);
        return false;
    }
    if(C.TemplateAsset) return BuildTemplate(C,B,Out,Cells);
    if (C.Floors<1 || C.Floors>3 || (C.Floors>1 && C.BranchLength<250)) return false;
    FERProceduralLayout L;
    const float Stride=B.Height+20;
    for (int32 Level=0;Level<C.Floors;++Level)
    {
        FERProceduralConfig Single=C;Single.Floors=C.Floors;Single.Seed=int32(uint32(C.Seed)+uint32(Level)*7919u);
        FERProceduralLayout Part;if (!BuildLevel(Single,B,Part,Level<C.Floors-1,Level>0,Cells)) return false;
        const float Base=Level*Stride;const int32 WallOffset=L.Walls.Num(),RoomOffset=L.Rooms.Num();
        for (auto R:Part.Rooms){R.Z=Base;R.Level=Level;R.Room+=RoomOffset;L.Rooms.Add(R);}
        for (auto R:Part.Corridors){R.Z=Base;R.Level=Level;L.Corridors.Add(R);}
        for (auto W:Part.Walls){W.Start.Z+=Base;L.Walls.Add(W);}
        for (auto O:Part.Openings){O.Wall+=WallOffset;O.Sill+=Base;L.Openings.Add(O);}
        for (auto Route:Part.Routes){for(auto& P:Route)P.Z+=Base;L.Routes.Add(MoveTemp(Route));}
        // Upper walls rest on their static slab: bottom rows stay anchored.
        for (auto P:Part.Panels){P.Id+=WallOffset*8;P.Transform.AddToTranslation(FVector(0,0,Base));L.Panels.Add(MoveTemp(P));}
        if (Level==0)L.Spawn=Part.Spawn;
    }
    // A straight 20cm-rise stair with an adjacent return lane. Lower hall
    // ceilings are shared upper slabs; the tower opening provides headroom.
    const float LaneMin=C.CorridorWidth*.5+50,LaneMax=LaneMin+150,LaneY=(LaneMin+LaneMax)*.5;
    const int32 Count=FMath::CeilToInt(Stride/20);const float Rise=Stride/Count,End=-450-Count*30;
    for(int32 Level=0;Level<C.Floors-1;++Level)
    {
        const float Base=Level*Stride,Top=Base+Stride;
        TArray<FVector> Route{FVector(-400,0,Base+100)};
        for(int32 I=0;I<Count;++I)
        {
            const float Height=(I+1)*Rise;const float X=-450-I*30-15;
            L.Steps.Add(FTransform(FQuat::Identity,FVector(X,0,Base+Height-Rise*.5),FVector(30,150,Rise)/100));
            Route.Add(FVector(X,0,Base+Height+100));
        }
        const float Run=Count*30;
        L.StairSupports.Add(FTransform(FRotator(-FMath::RadiansToDegrees(FMath::Atan2(Stride,Run)),0,0),FVector(-450-Run*.5,0,Base+Stride*.5-12),FVector(FMath::Sqrt(Run*Run+Stride*Stride),150,12)/100));
        // Disjoint upper landing, return lane and near-hall connector.
        L.Corridors.Add({FVector2D(End-150,-75),FVector2D(End,LaneMax),INDEX_NONE,Top,Level+1});
        L.Corridors.Add({FVector2D(End,LaneMin),FVector2D(-350,LaneMax),INDEX_NONE,Top,Level+1});
        L.Corridors.Add({FVector2D(-450,C.CorridorWidth*.5),FVector2D(-350,LaneMin),INDEX_NONE,Top,Level+1});
        Route.Append({FVector(End-75,0,Top+100),FVector(End-75,LaneY,Top+100),FVector(-400,LaneY,Top+100),FVector(-400,0,Top+100)});
        L.StairRoutes.Add(MoveTemp(Route));
    }
    Out=MoveTemp(L);return true;
}
bool ERProcedural::Build(const FERProceduralConfig& C,const FERBlockRoomConfig& B,FERProceduralLayout& Out,const FERCellSettings& Cells)
{
    if(C.ExpectedTeamSize<0 || C.ExpectedTeamSize>16)return false;
    if(C.TemplateAsset && C.ExpectedTeamSize>0)
    {
        TStrongObjectPtr<UERMapTemplate> Recipe(DuplicateObject<UERMapTemplate>(C.TemplateAsset,GetTransientPackage()));
        // Bounded deterministic composition retries, with unchanged requested
        // seed and cosmetic stream. Every peer selects the same accepted plan.
        const int32 Attempts=C.TemplateAsset->Archetype==EERMapArchetype::MiddleEasternAlleys ? 1024:16;
        const uint32 Plan=C.MapCandidate ? HashCombine(uint32(C.Seed),uint32(C.MapCandidate)*0x2f0b4a87u):uint32(C.Seed);
        int32 GeometryRejected=0,SpawnRejected=0;
        FERProceduralLayout BestSpawnFit;int32 BestSpawnZones=0;
        const int32 RequestedZones=1+uint32(C.Seed)%3;
        for(int32 Attempt=0;Attempt<Attempts;++Attempt)
        {
            FERProceduralConfig Sized=C;
            Sized.Seed=Attempt ? int32(HashCombine(Plan,uint32(Attempt)*0x9e3779b9u)):int32(Plan);
            if(!ConfigureTeamSize(*Recipe,C.ExpectedTeamSize,Sized.Seed))return false;
            Recipe->CompositionSeed=C.Seed;
            Sized.TemplateAsset=Recipe.Get();FERProceduralLayout Next;
            if(!BuildGeometry(Sized,B,Next,Cells)){++GeometryRejected;continue;}
            if(Recipe->Archetype==EERMapArchetype::Rooms)
            {
                // Fit room dimensions to the actual primary floor, keeping
                // corridor widths, heights, materials and cells unchanged.
                for(int32 Fit=0;Fit<8;++Fit)
                {
                    const double Actual=PlayableAreaM2(Next),Target=TargetAreaM2(C.ExpectedTeamSize);
                    if(Actual<=0 || FMath::Abs(Actual-Target)<=Target*.05)break;
                    Recipe->Rooms.NominalRoomSize=FMath::GridSnap(
                        Recipe->Rooms.NominalRoomSize*float(FMath::Sqrt(Target/Actual)),50.f);
                    if(!BuildGeometry(Sized,B,Next,Cells)){Next={};break;}
                }
            }
            if(C.TemplateAsset->Archetype!=EERMapArchetype::LegacyAssembly && FMath::Abs(PlayableAreaM2(Next)-TargetAreaM2(C.ExpectedTeamSize))>TargetAreaM2(C.ExpectedTeamSize)*.1+.01)continue;
            if(!BuildSpawnZones(C.Seed,Next,Cells)){++SpawnRejected;continue;}
            if(!ERPlayability::InitialSpawnsOccluded(Next)){++SpawnRejected;continue;}
            const int32 Zones=Next.SpawnZones.Num()/2;
            if(Zones<RequestedZones && Attempt<7)
            {
                if(Zones>BestSpawnZones){BestSpawnFit=MoveTemp(Next);BestSpawnZones=Zones;}
                continue;
            }
            if(BestSpawnZones>Zones)Next=MoveTemp(BestSpawnFit);
            if(!ApplyMapTheme(C,Next))return false;
            Out=MoveTemp(Next);return true;
        }
        if(BestSpawnZones>0)
        {
            if(!ApplyMapTheme(C,BestSpawnFit))return false;
            Out=MoveTemp(BestSpawnFit);return true;
        }
        UE_LOG(LogTemp,Warning,TEXT("ER_MAP_BUILD_EXHAUSTED seed=%d team=%d geometry_rejected=%d spawn_rejected=%d"),C.Seed,C.ExpectedTeamSize,GeometryRejected,SpawnRejected);
        return false;
    }
    FERProceduralLayout Next;
    if(!BuildGeometry(C,B,Next,Cells) || !ApplyMapTheme(C,Next))return false;
    Out=MoveTemp(Next);return true;
}

bool ERProcedural::ApplyMapTheme(const FERProceduralConfig& C,FERProceduralLayout& L)
{
    if(!C.TemplateAsset || !C.TemplateAsset->ThemeLibrary)return true;
    const auto& Library=*C.TemplateAsset->ThemeLibrary;
    if(Library.Revision<1 || Library.Themes.IsEmpty() || Library.Themes.Num()>8)return false;
    TSet<FName> Names;
    for(const auto& P:Library.Themes)
    {
        if(P.Name.IsNone() || Names.Contains(P.Name) || P.Materials.Num()!=7 || !P.Wall || !P.Floor || !P.Ceiling || P.Grounds.IsEmpty() || P.Grounds.Num()>32)return false;
        for(const auto& M:P.Materials)if(!M)return false;
        for(const auto& M:P.Grounds)if(!M)return false;
        const int32 SteelRoles=int32(P.ShellSteel!=nullptr)+int32(P.LadderSteel!=nullptr)+int32(P.DeckSteel!=nullptr);
        if(SteelRoles!=0 && SteelRoles!=3)return false;
        Names.Add(P.Name);
    }
    // Each template owns its library; the cosmetic stream stays separate from geometry RNG.
    FRandomStream Cosmetic(int32(HashCombine(uint32(C.Seed),0x524f4f4du)));
    const bool Village=C.TemplateAsset->Archetype==EERMapArchetype::Village;
    const int32 Index=Cosmetic.RandRange(0,Library.Themes.Num()-1);
    const auto& P=Library.Themes[Index];
    L.ThemeIndex=Index;L.ThemeRevision=Library.Revision;L.ThemeName=P.Name;L.ThemeCeiling=P.Ceiling;
    L.ThemeMetalDetails=P.LadderSteel;
    L.ThemeMaterials.Reset();for(const auto& M:P.Materials)L.ThemeMaterials.Add(M.Get());
    for(auto& Panel:L.Panels)
    {
        const bool Cargo=P.PreserveCargoPaint && Panel.Material==EERBlockMaterial::Metal && Panel.Strength>1 && Panel.Surface;
        // Jungle retains Village's original UV skins and gray cladding.
        const bool VillageSkin=Village && P.Name==TEXT("Jungle") && Panel.Surface;
        if(!Cargo && !VillageSkin)Panel.Surface=P.Materials[uint8(Panel.Material)];
        // House walls show their physical material; slabs keep the screed finish.
        const FString Role=Panel.SkinName.ToString();
        if(Role.StartsWith(TEXT("AlleyModule_")) && (Role.EndsWith(TEXT("_Floor")) || Role.EndsWith(TEXT("_Roof"))))Panel.Surface=P.Floor;
        if(P.LadderSteel && Panel.Material==EERBlockMaterial::Metal && Panel.Strength==1)
        {
            const FVector Center=Panel.Transform.TransformPosition(Panel.Size*.5);
            // Ladder rails and rungs already have authoritative local frames;
            // select their finish without changing cell type or geometry.
            for(const auto& Ladder:L.Ladders)
            {
                const FVector Local=Ladder.Frame.InverseTransformPosition(Center);
                if(FMath::Abs(Local.X)<=5 && FMath::Abs(Local.Y)<=47 && Local.Z>=0 && Local.Z<=Ladder.DeckHeight+111)
                {Panel.Surface=P.LadderSteel;break;}
            }
        }
        if(!Panel.Skin.IsEmpty())Panel.InteriorSurface=VillageSkin ? P.Materials[uint8(Panel.Material)].Get():Panel.Surface;
    }
    L.GroundSurface=P.Grounds[Cosmetic.RandRange(0,P.Grounds.Num()-1)];
    for(auto& Piece:L.FixedPieces)
    {
        if(Village && Piece.VisualOnly)
        {
            if(!Piece.Mesh)Piece.Surface=L.GroundSurface; // match the playable floor
            continue; // foliage retains its own material slots
        }
        // Preserve emissive fixtures and their authored material slots.
        if(Piece.CollisionOnly || (Piece.VisualOnly && !Piece.Mesh) || (Piece.Mesh && Piece.Mesh->GetName().Contains(TEXT("Lamp"))))continue;
        Piece.Surface=Piece.Roof ? P.Ceiling.Get():P.Materials[uint8(Piece.Material)].Get();
        const FVector FixedSize=Piece.Transform.GetScale3D()*100;
        const bool ProceduralShell=C.TemplateAsset->Archetype==EERMapArchetype::FireTraining && !Piece.Mesh && !Piece.Roof && FixedSize.Z>=700 && (FixedSize.X>=L.WarehouseWidth-50 || FixedSize.Y>=L.WarehouseLength-1);
        if(ProceduralShell)Piece.Surface=Piece.Material==EERBlockMaterial::Concrete ? P.Wall.Get():P.ShellSteel ? P.ShellSteel.Get():Piece.Surface;
        else if(P.ShellSteel && Piece.Mesh && Piece.Mesh->GetName().StartsWith(TEXT("SM_ER_WH_")))
        {
            const FString Name=Piece.Mesh->GetName();
            Piece.Surface=Name.Contains(TEXT("Post")) || Name.Contains(TEXT("RoofArch")) ? P.Materials[uint8(EERBlockMaterial::Metal)].Get():Piece.Roof ? P.Ceiling.Get():P.ShellSteel.Get();
        }
        else if(P.LadderSteel && Piece.PawnBarrier)Piece.Surface=P.LadderSteel;
        else if(P.DeckSteel && !Piece.Mesh && !Piece.VisualOnly && !Piece.Roof)
        {
            const FVector Size=Piece.Transform.GetScale3D()*100;
            if(Size.Z<=30 && Size.X>=100 && Size.Y>=100 && Piece.Material==EERBlockMaterial::Metal)Piece.Surface=P.DeckSteel;
        }
    }
    for(auto& Face:L.RoomShell)
    {
        const FVector Normal=FVector::CrossProduct(Face.B-Face.A,Face.C-Face.A).GetSafeNormal();
        Face.Surface=Face.Roof ? P.Ceiling.Get():Normal.Z>.9 ? P.Floor.Get():P.Wall.Get();
    }
    L.RoomsTheme=Index;
    return true;
}
void ERProcedural::AddPieceCells(int32 Id, EERBlockMaterial Material, const FBox& Box, bool bAnchoredBase, const FERCellSettings& Cells, TArray<FERCellPanelDesc>& Out)
{
    // Exact validated volume: each axis holds round(L/CellSize) equal cells, so stacked
    // pieces keep touching (Source-derived sizes are rarely whole 5cm multiples).
    const FBox& B=Box;
    const FVector Size=B.GetSize();
    if (Size.GetMin()<.1) return;
    const bool bGround=B.Min.Z<=.01, bThin=Material==EERBlockMaterial::Wood || Material==EERBlockMaterial::Drywall || Material==EERBlockMaterial::Metal || Material==EERBlockMaterial::Glass;
    const int32 First=Out.Num();
    if (!bThin || FMath::Min(Size.X,Size.Y)<=30)
    {
        // Slab or wall: X along the longer horizontal side.
        if (Size.X>=Size.Y) FERCellWorld::AddConstruction(Id,Material,FTransform(B.Min),Size,Cells,Out);
        else FERCellWorld::AddConstruction(Id,Material,FTransform(FRotator(0,90,0),FVector(B.Max.X,B.Min.Y,B.Min.Z)),FVector(Size.Y,Size.X,Size.Z),Cells,Out);
        for (int32 P=First;P<Out.Num();++P) Out[P].bAnchorBottom=bGround || bAnchoredBase;
        return;
    }
    // Hollow crate: boards of the construction thickness, never a solid block of wood.
    const float W=Material==EERBlockMaterial::Wood ? Cells.WoodDepth : Material==EERBlockMaterial::Drywall ? Cells.DrywallBoard : Material==EERBlockMaterial::Metal ? Cells.SheetThickness : Cells.GlassThickness;
    auto Board=[&](int32 Layer, const FBox& Part, int32 Thin, bool bBottom)
    {
        const FVector S=Part.GetSize();
        FERCellPanelDesc& P=Out.AddDefaulted_GetRef();
        P.Id=Id*8+Layer; P.Material=Material; P.bAnchorBottom=bBottom;
        // Local Y is the thin axis; frames are right-handed rotations.
        if (Thin==1) { P.Transform=FTransform(Part.Min); P.Size=S; }
        else if (Thin==0) { P.Transform=FTransform(FMatrix(FVector(0,1,0),FVector(-1,0,0),FVector(0,0,1),FVector(Part.Max.X,Part.Min.Y,Part.Min.Z))); P.Size=FVector(S.Y,S.X,S.Z); }
        else { P.Transform=FTransform(FMatrix(FVector(1,0,0),FVector(0,0,1),FVector(0,-1,0),FVector(Part.Min.X,Part.Max.Y,Part.Min.Z))); P.Size=FVector(S.X,S.Z,S.Y); }
        P.Transform.SetScale3D(FVector::OneVector);
        P.Cell=FVector(FERCellWorld::FitCell(P.Size.X,Cells.CellSize),P.Size.Y,FERCellWorld::FitCell(P.Size.Z,Cells.CellSize));
    };
    const bool bBottom=bGround || bAnchoredBase;
    // Side tops meet the roof underside. Overlapping thin shells have no
    // external face contact, leaving the roof unsupported in the cell graph.
    Board(0,FBox(B.Min,FVector(B.Max.X,B.Min.Y+W,B.Max.Z-W)),1,bBottom);
    Board(1,FBox(FVector(B.Min.X,B.Max.Y-W,B.Min.Z),FVector(B.Max.X,B.Max.Y,B.Max.Z-W)),1,bBottom);
    Board(2,FBox(B.Min,FVector(B.Min.X+W,B.Max.Y,B.Max.Z-W)),0,bBottom);
    Board(3,FBox(FVector(B.Max.X-W,B.Min.Y,B.Min.Z),FVector(B.Max.X,B.Max.Y,B.Max.Z-W)),0,bBottom);
    Board(4,FBox(FVector(B.Min.X,B.Min.Y,B.Max.Z-W),B.Max),2,false);
}
AERProceduralMap::AERProceduralMap()
{
    auto Create=[&](const TCHAR* Name)
    {
        auto* Mesh=CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
        Mesh->SetupAttachment(RootComponent); Mesh->SetCollisionProfileName(TEXT("BlockAll"));
        Mesh->SetCanEverAffectNavigation(false); return Mesh;
    };
    TileFloors=Create(TEXT("TileFloors")); WoodFloors=Create(TEXT("WoodFloors")); Ceilings=Create(TEXT("Ceilings")); Stairs=Create(TEXT("IndestructibleStairs"));
    FixedCollision=Create(TEXT("FixedTemplateCollision"));FixedCollision->SetHiddenInGame(true);FixedCollision->SetCastShadow(false);
    FenceCollision=Create(TEXT("FixedFenceCollision"));FenceCollision->SetHiddenInGame(true);FenceCollision->SetCastShadow(false);
    // This is a locomotion volume, not solid WorldStatic cover. Object-type
    // blast queries must see the perforated metal instead of this full box.
    FenceCollision->SetCollisionObjectType(ECC_Pawn);
    FenceCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
    FenceCollision->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);
    FenceCollision->SetCollisionResponseToChannel(ECC_WorldDynamic,ECR_Block);
}
bool AERProceduralMap::GenerateLogicalWorld()
{
    ResolveMapCandidate();
    FERProceduralLayout Next;
    if (!ERProcedural::Build(LayoutConfig,Config,Next,CellSettings) || !BuildCellWorld(Next.Panels)) return false;
    PlayabilityDraft.Reset();
    if(HasAuthority() && ERPlayability::Mode()==EERPlayabilityMode::ReportOnly && LayoutConfig.TemplateAsset)
    {
        const auto Policy=ERPlayability::PolicyFor(LayoutConfig.TemplateAsset->Archetype,LayoutConfig.ExpectedTeamSize);
        PlayabilityDraft=MakeShared<FERPlayabilityReport>(ERPlayability::Analyze(Next,LayoutConfig,Policy));
        const double Clock=FPlatformTime::Seconds();
        PlayabilityDraft->Stress=ERPlayability::Stress(Next,*PlayabilityDraft,Policy);
        PlayabilityDraft->TimingsMs.Emplace(TEXT("stress"),(FPlatformTime::Seconds()-Clock)*1000);
    }
    // Live cell state belongs to the cell world; retain only layout metadata.
    Next.Panels.Empty();
    Layout=MoveTemp(Next); return true;
}
void AERProceduralMap::ResolveMapCandidate()
{
    if(!HasAuthority())return;
    if(!bSelectMapCandidates || !LayoutConfig.TemplateAsset){LayoutConfig.MapCandidate=0;return;}
    const FIntPoint Request(LayoutConfig.Seed,LayoutConfig.ExpectedTeamSize);
    if(SelectedFor.IsSet() && SelectedFor.GetValue()==Request)return;
    const auto Profile=ERMapSelection::ProfileFor(LayoutConfig.TemplateAsset->Archetype,LayoutConfig.ExpectedTeamSize);
    const auto Selection=ERMapSelection::Select(LayoutConfig,Config,CellSettings,Profile);
    LayoutConfig.MapCandidate=FMath::Max(0,Selection.Chosen);SelectedFor=Request;
    UE_LOG(LogTemp,Display,TEXT("ER_MAP_SELECTION seed=%d template=%s %s"),LayoutConfig.Seed,*LayoutConfig.TemplateAsset->TemplateId,*Selection.Line());
}
bool AERProceduralMap::ValidateGeneratedCollision() const
{
    return !LayoutConfig.TemplateAsset || LayoutConfig.ExpectedTeamSize==0 ||
        (ERPlayability::InitialSpawnsOccludedInWorld(GetWorld(),GetActorTransform(),Layout) &&
         ERProcedural::SpawnZonesClearInWorld(GetWorld(),GetActorTransform(),Layout));
}
void AERProceduralMap::OnGenerationFinished()
{
    if(!PlayabilityDraft.IsValid())return;
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("Testing/MapPlayability");
    ERPlayability::Save(*PlayabilityDraft,Directory);
    ERPlayability::ConfirmInWorld(*PlayabilityDraft,GetWorld(),GetActorTransform(),Layout,ERPlayability::PolicyFor(LayoutConfig.TemplateAsset->Archetype,LayoutConfig.ExpectedTeamSize));
    ERPlayability::Save(*PlayabilityDraft,Directory);
    UE_LOG(LogTemp,Display,TEXT("ER_MAP_PLAYABILITY_LIVE %s"),*ERPlayability::Summary(*PlayabilityDraft));
    PlayabilityDraft.Reset();
}
void AERProceduralMap::ConfigureFloors()
{
    for(auto Ladder:LadderActors)if(Ladder)Ladder->Destroy();
    LadderActors.Reset();
    for(auto& Light:WarehouseLights)if(Light){RemoveInstanceComponent(Light);Light->DestroyComponent();}
    WarehouseLights.Reset();
    for(auto& Shell:RoomsShell)
    {
        RemoveInstanceComponent(Shell);Shell->DestroyComponent();
        Shell->Rename(*MakeUniqueObjectName(this,UDynamicMeshComponent::StaticClass(),TEXT("DeadRoomsShell")).ToString(),nullptr,REN_DontCreateRedirectors|REN_NonTransactional);
    }
    RoomsShell.Reset();
    if(AlleyTerrain)
    {
        RemoveInstanceComponent(AlleyTerrain);AlleyTerrain->DestroyComponent();
        AlleyTerrain->Rename(*MakeUniqueObjectName(this,UDynamicMeshComponent::StaticClass(),TEXT("DeadAlleyTerrain")).ToString(),nullptr,REN_DontCreateRedirectors|REN_NonTransactional);
        AlleyTerrain=nullptr;
    }
    for(auto& Mesh:FixedMeshes)
    {
        RemoveInstanceComponent(Mesh); Mesh->DestroyComponent();
        // Free the deterministic name before the replacement group claims it.
        Mesh->Rename(*MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), TEXT("DeadTemplateFixed")).ToString(), nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
    }
    FixedMeshes.Empty();FixedCollision->ClearInstances();FenceCollision->ClearInstances();Ceilings->SetHiddenInGame(false);
    for(auto& Metal:FenceMetal)
    {
        RemoveInstanceComponent(Metal);Metal->DestroyComponent();
        Metal->Rename(*MakeUniqueObjectName(this,UDynamicMeshComponent::StaticClass(),TEXT("DeadFenceMetal")).ToString(),nullptr,REN_DontCreateRedirectors|REN_NonTransactional);
    }
    FenceMetal.Empty();
    if(LayoutConfig.TemplateAsset) {ConfigureTemplateFloors(); return;}
    // A foundation touches the underside of the 20cm interior floor slabs.
    // Its exposed asphalt apron is one walkable step down after an outer breach.
    FVector2D Min=Layout.Corridors[0].Min, Max=Layout.Corridors[0].Max;
    for (const auto& R:Layout.Rooms) { Min.X=FMath::Min(Min.X,R.Min.X);Min.Y=FMath::Min(Min.Y,R.Min.Y);Max.X=FMath::Max(Max.X,R.Max.X);Max.Y=FMath::Max(Max.Y,R.Max.Y); }
    Floor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Floor->SetVisibility(true);
    Floor->SetRelativeLocation(FVector((Min.X+Max.X)*.5f,(Min.Y+Max.Y)*.5f,-30));
    Floor->SetRelativeScale3D(FVector(Max.X-Min.X+400,Max.Y-Min.Y+400,20)/100);
    if (ExteriorFloorSurface) Floor->SetMaterial(0,ExteriorFloorSurface);
    for (auto* Mesh : {TileFloors.Get(),WoodFloors.Get(),Ceilings.Get(),Stairs.Get()}) { Mesh->ClearInstances(); Mesh->SetStaticMesh(BlockMesh); }
    if (FloorSurface) TileFloors->SetMaterial(0,FloorSurface);
    if (WoodFloorSurface) WoodFloors->SetMaterial(0,WoodFloorSurface);
    if (CeilingSurface) Ceilings->SetMaterial(0,CeilingSurface);
    auto Add=[&](const FERLayoutRect& R, UInstancedStaticMeshComponent* Mesh)
    {
        const FVector Center((R.Min.X+R.Max.X)*.5f,(R.Min.Y+R.Max.Y)*.5f,R.Z-10);
        const FVector Scale(R.Max.X-R.Min.X,R.Max.Y-R.Min.Y,20);
        Mesh->AddInstance(FTransform(FQuat::Identity,Center,Scale/100));
        if (bCeilings && R.Level==LayoutConfig.Floors-1) Ceilings->AddInstance(FTransform(FQuat::Identity,FVector(Center.X,Center.Y,R.Z+Config.Height+5),FVector(Scale.X,Scale.Y,10)/100));
    };
    for (const auto& R:Layout.Rooms) Add(R,R.Room%2 ? WoodFloors.Get() : TileFloors.Get());
    for (const auto& R:Layout.Corridors) Add(R,TileFloors);
    if (FloorSurface) Stairs->SetMaterial(0,FloorSurface);
    for (const auto& T:Layout.Steps) Stairs->AddInstance(T);
    for (const auto& T:Layout.StairSupports) Stairs->AddInstance(T);
}
FString AERProceduralMap::GetLayoutStatistics() const
{
    if(LayoutConfig.TemplateAsset) return FString::Printf(TEXT("template=%s rev=%d seed=%d theme=%s arena_m=%.2fx%.2f rooms=%d modules=%d fixed=%d starts=%d/%d macro=%d candidate=%d"),*LayoutConfig.TemplateAsset->TemplateId,LayoutConfig.TemplateAsset->Revision,LayoutConfig.Seed,*Layout.ThemeName.ToString(),Layout.ArenaSize.X/100,Layout.ArenaSize.Y/100,Layout.Rooms.Num(),Layout.ModuleKinds.Num()*2,Layout.FixedPieces.Num(),Layout.AlphaSpawns.Num(),Layout.OmegaSpawns.Num(),Layout.RoomsMacroVariant,LayoutConfig.MapCandidate);
    return FString::Printf(TEXT("seed=%d rooms=%d corridors=%d openings=%d floors=%d steps=%d"),LayoutConfig.Seed,Layout.Rooms.Num(),Layout.Corridors.Num(),Layout.Openings.Num(),LayoutConfig.Floors,Layout.Steps.Num());
}

SIZE_T AERProceduralMap::GetLayoutBytes() const
{
    SIZE_T Bytes=Layout.FixedPieces.GetAllocatedSize()+Layout.AlphaSpawns.GetAllocatedSize()+Layout.OmegaSpawns.GetAllocatedSize()+Layout.Rooms.GetAllocatedSize()+Layout.Corridors.GetAllocatedSize()+Layout.Walls.GetAllocatedSize()+Layout.Openings.GetAllocatedSize()+Layout.Routes.GetAllocatedSize()+Layout.Panels.GetAllocatedSize()+Layout.Steps.GetAllocatedSize()+Layout.StairSupports.GetAllocatedSize()+Layout.StairRoutes.GetAllocatedSize();
    for(const auto& Route:Layout.StairRoutes) Bytes+=Route.GetAllocatedSize();
    Bytes+=Layout.VillageSideWalls.GetAllocatedSize()+Layout.VillageSideBypasses.GetAllocatedSize();
    for(const auto& Route:Layout.VillageSideBypasses)Bytes+=Route.GetAllocatedSize();
    Bytes+=Layout.VillageBlocks.GetAllocatedSize()+Layout.VillageFloorHoles.GetAllocatedSize()+Layout.VillageUpperLadders.GetAllocatedSize()+Layout.VillageAccessFlights.GetAllocatedSize();
    Bytes+=Layout.VillageClosedGround.GetAllocatedSize()+Layout.VillagePassages.GetAllocatedSize();
    for(const auto& Polygon:Layout.VillageClosedGround)Bytes+=Polygon.GetAllocatedSize();
    for(const auto& Route:Layout.VillagePassages)Bytes+=Route.GetAllocatedSize();
    Bytes+=Layout.RoomNodes.GetAllocatedSize()+Layout.RoomLinks.GetAllocatedSize()+Layout.RoomShell.GetAllocatedSize()+Layout.RoomLightPoints.GetAllocatedSize();
    for(const auto& Node:Layout.RoomNodes)Bytes+=Node.Polygon.GetAllocatedSize();
    Bytes+=Layout.Bases.GetAllocatedSize()+Layout.ModuleKinds.GetAllocatedSize()+Layout.RoomAccessRoutes.GetAllocatedSize();
    Bytes+=Layout.TownBuildings.GetAllocatedSize()+Layout.TownCourts.GetAllocatedSize()+Layout.TownGates.GetAllocatedSize()+Layout.TownStreetNames.GetAllocatedSize()+Layout.UpperRoute.GetAllocatedSize()+Layout.LowerRoute.GetAllocatedSize();
    for(const auto& Gate:Layout.TownGates)Bytes+=Gate.Passage.GetAllocatedSize();
    for(const auto& Route:Layout.RoomAccessRoutes)Bytes+=Route.GetAllocatedSize();
    Bytes+=Layout.Houses.GetAllocatedSize()+Layout.TerrainPads.GetAllocatedSize()+Layout.Ladders.GetAllocatedSize()+Layout.DistrictTowers.GetAllocatedSize()+Layout.DistrictTowerHeights.GetAllocatedSize();
    for(const auto& House:Layout.Houses)
    {
        Bytes+=House.Entrances.GetAllocatedSize()+House.EntranceFaces.GetAllocatedSize()+House.RoofMouths.GetAllocatedSize();
        for(const auto& Entry:House.Entrances)Bytes+=Entry.GetAllocatedSize();
    }
    for(const auto& Route:Layout.RoofTransfers)Bytes+=Route.GetAllocatedSize();
    Bytes+=Layout.UnderpassRamps.GetAllocatedSize()+Layout.UnderpassBreaches.GetAllocatedSize()+Layout.LowCovers.GetAllocatedSize()+Layout.UnderpassRoute.GetAllocatedSize()+Layout.UnderpassApproach.GetAllocatedSize();
    for(const auto* Routes:{&Layout.RooftopRoutes,&Layout.GroundRoutes,&Layout.DistrictRoutes})
    {Bytes+=Routes->GetAllocatedSize();for(const auto& Route:*Routes)Bytes+=Route.GetAllocatedSize();}
    Bytes+=Layout.CrossingRoutes.GetAllocatedSize()+Layout.ClimbPoints.GetAllocatedSize();
    Bytes+=Layout.CoverSites.GetAllocatedSize()+Layout.Containers.GetAllocatedSize()+Layout.SecondaryStructures.GetAllocatedSize()+Layout.CoverAngles.GetAllocatedSize()+Layout.WallAccessFlights.GetAllocatedSize()+Layout.WallBridges.GetAllocatedSize();
    for(const auto& Route:Layout.CrossingRoutes)Bytes+=Route.GetAllocatedSize();
    for (const auto& Route:Layout.Routes) Bytes+=Route.GetAllocatedSize();
    return Bytes;
}

void AERProceduralMap::ConfigureTemplateFloors()
{
    const auto* Template=LayoutConfig.TemplateAsset.Get();const FVector Size=Layout.ArenaSize;
    const bool ClosedRooms=Template->Archetype==EERMapArchetype::Rooms;
    Floor->SetStaticMesh(Layout.GradedTerrain || ClosedRooms ? nullptr:BlockMesh.Get());Floor->SetRelativeLocation(FVector(0,0,-10));Floor->SetRelativeScale3D(FVector(Size.X+40,Size.Y+40,20)/100);
    Floor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Floor->SetVisibility(true);
    if(ClosedRooms){Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);Floor->SetVisibility(false);}
    if(auto* Surface=Layout.GroundSurface ? Layout.GroundSurface:FloorSurface.Get())Floor->SetMaterial(0,Surface);
    if(Layout.GradedTerrain)
    {
        AlleyTerrain=NewObject<UDynamicMeshComponent>(this,TEXT("AlleyTerrain"));AddInstanceComponent(AlleyTerrain);
        AlleyTerrain->SetMobility(EComponentMobility::Movable);AlleyTerrain->SetNetAddressable();AlleyTerrain->SetupAttachment(RootComponent);
        AlleyTerrain->SetCanEverAffectNavigation(false);AlleyTerrain->SetDeferredCollisionUpdatesEnabled(true,false);
        AlleyTerrain->SetComplexAsSimpleCollisionEnabled(true,false);AlleyTerrain->SetCollisionProfileName(TEXT("BlockAll"));
        AlleyTerrain->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);
        AlleyTerrain->SetMaterial(0,Layout.GroundSurface);AlleyTerrain->RegisterComponent();if(Layout.CircularArena)ERProcedural::BuildCentralTerrain(AlleyTerrain,Layout);else if(!Layout.VillageTerrainProfile.IsEmpty())ERProcedural::BuildVillageTerrain(AlleyTerrain,Layout);else ERProcedural::BuildAlleyTerrain(AlleyTerrain,Layout);
    }
    for(auto* Mesh:{TileFloors.Get(),WoodFloors.Get(),Ceilings.Get(),Stairs.Get()}){Mesh->ClearInstances();Mesh->SetStaticMesh(BlockMesh);}
    FixedCollision->SetStaticMesh(BlockMesh);
    FenceCollision->SetStaticMesh(BlockMesh);
    UInstancedStaticMeshComponent* FenceModules=nullptr;
    const bool FireTraining=Template->Archetype==EERMapArchetype::FireTraining;
    if(auto* Module=FireTraining ? nullptr:Template->CentralBuilding.FenceModule.Get())
    {
#if WITH_EDITOR
        // First editor load may still be building the imported collision mesh.
        // The barrier must be ready for queries as soon as the layout is ready.
        FStaticMeshCompilingManager::Get().FinishCompilation({Module});
#endif
        if(auto* Body=Module->GetBodySetup())Body->CreatePhysicsMeshes();
        FenceModules=NewObject<UInstancedStaticMeshComponent>(this,TEXT("IndustrialFenceModules"));
        AddInstanceComponent(FenceModules);FenceModules->SetMobility(EComponentMobility::Movable);
        FenceModules->SetNetAddressable();FenceModules->SetupAttachment(RootComponent);
        FenceModules->SetStaticMesh(Module);FenceModules->SetCanEverAffectNavigation(false);
        FenceModules->SetCollisionProfileName(TEXT("BlockAll"));
        FenceModules->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
        FenceModules->SetCollisionResponseToChannel(ECC_WorldDynamic,ECR_Ignore);
        if(auto* Steel=(Layout.ThemeMetalDetails ? Layout.ThemeMetalDetails:Layout.ThemeIndex!=INDEX_NONE ? Layout.ThemeMaterials[uint8(EERBlockMaterial::Metal)]:FireTraining ? Template->FireTraining.SteelSurface.Get():Template->CentralBuilding.SteelSurface.Get()))FenceModules->SetMaterial(0,Steel);
        FenceModules->RegisterComponent();FixedMeshes.Add(FenceModules);
    }
    UE::Geometry::FDynamicMesh3 Metal;Metal.EnableAttributes();
    auto FlushMetal=[&]()
    {
        if(Metal.TriangleCount()==0)return;
        // Bound each cooked mesh well below Unreal's complex-collision limit.
        // Cooking is synchronous, so no client can open a gate on missing wire.
        auto* Part=NewObject<UDynamicMeshComponent>(this,*FString::Printf(TEXT("IndustrialFenceMetal_%d"),FenceMetal.Num()));
        AddInstanceComponent(Part);Part->SetMobility(EComponentMobility::Movable);Part->SetNetAddressable();
        Part->SetupAttachment(RootComponent);Part->SetCanEverAffectNavigation(false);
        Part->SetDeferredCollisionUpdatesEnabled(true,false);Part->SetComplexAsSimpleCollisionEnabled(true,false);
        Part->SetCollisionProfileName(TEXT("BlockAll"));
        Part->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);Part->SetCollisionResponseToChannel(ECC_WorldDynamic,ECR_Ignore);
        Part->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);
        Part->SetMaterial(0,Layout.ThemeMetalDetails ? Layout.ThemeMetalDetails:Layout.ThemeIndex!=INDEX_NONE ? Layout.ThemeMaterials[uint8(EERBlockMaterial::Metal)]:Template->CentralBuilding.SteelSurface ? Template->CentralBuilding.SteelSurface.Get():MaterialCatalog->Find(EERBlockMaterial::Metal)->Surface.Get());
        Part->RegisterComponent();Part->SetMesh(MoveTemp(Metal));Part->UpdateCollision(false);FenceMetal.Add(Part);
        Metal=UE::Geometry::FDynamicMesh3();Metal.EnableAttributes();
    };
    Ceilings->SetHiddenInGame(Template->RoofMesh!=nullptr || Template->Archetype==EERMapArchetype::FireTraining);
    if(auto* Finish=Layout.ThemeCeiling ? Layout.ThemeCeiling:CeilingSurface.Get())Ceilings->SetMaterial(0,Finish);
    if(bCeilings && !ClosedRooms && !FireTraining)Ceilings->AddInstance(FTransform(FQuat::Identity,FVector(0,0,Size.Z+10),FVector(Size.X+40,Size.Y+40,20)/100));
    if(ClosedRooms)ERProcedural::BuildRoomsShell(this,Layout,RoomsShell);
    for(const auto& Piece:Layout.FixedPieces)
    {
        if(Template->Archetype==EERMapArchetype::Village && Piece.VisualOnly && GetNetMode()==NM_DedicatedServer)continue;
        if(Piece.Roof && !bCeilings && !FireTraining)continue;
        if(Piece.CollisionOnly){(Piece.PawnBarrier ? FenceCollision:FixedCollision)->AddInstance(Piece.Transform);continue;}
        if(Piece.PawnBarrier)
        {
            FenceCollision->AddInstance(Piece.Transform);
            if(FenceModules)
            {
                TArray<FTransform> Instances;
                ERIndustrialFence::ModuleTransforms(Piece,FenceModules->GetStaticMesh()->GetBoundingBox(),Instances);
                for(const auto& Instance:Instances)FenceModules->AddInstance(Instance);
                continue;
            }
            ERIndustrialFence::Append(Piece,Metal);
            if(Metal.TriangleCount()>=50000)FlushMetal();
            continue;
        }
        UStaticMesh* Asset=Piece.Mesh ? Piece.Mesh:BlockMesh.Get();
        auto* Surface=Piece.Surface ? Piece.Surface:MaterialCatalog->Find(Piece.Material)->Surface.Get();
        auto* ExpectedSurface=Piece.Surface ? Piece.Surface:(Piece.Mesh ? Asset->GetMaterial(0):Surface);
        UInstancedStaticMeshComponent* Group=nullptr;
        const bool Visual=(!Piece.MeshCollision && Piece.Mesh) || Piece.VisualOnly || Piece.PawnBarrier;
        for(auto& Existing:FixedMeshes)if(Existing->GetStaticMesh()==Asset && Existing->ComponentHasTag(TEXT("ERTemplateRoof"))==Piece.Roof && Existing->ComponentHasTag(TEXT("ERTemplateVisual"))==Visual && Existing->GetMaterial(0)==ExpectedSurface){Group=Existing;break;}
        if(!Group)
        {
            // Groups are created in FixedPieces order, identical on host and clients:
            // pawns stand on them, so corrections carry them by name.
            Group=NewObject<UInstancedStaticMeshComponent>(this,*FString::Printf(TEXT("TemplateFixed_%d"),FixedMeshes.Num()));AddInstanceComponent(Group);Group->SetMobility(EComponentMobility::Movable);
            Group->SetNetAddressable();
            Group->SetupAttachment(RootComponent);Group->SetStaticMesh(Asset);Group->SetCollisionProfileName(Visual ? TEXT("NoCollision"):TEXT("BlockAll"));
            if(Visual)Group->ComponentTags.Add(TEXT("ERTemplateVisual"));
            if(Visual && Template->Archetype==EERMapArchetype::Village)
            {Group->SetCastShadow(false);if(Piece.Mesh && !Piece.Mesh->GetName().Contains(TEXT("SkyDome")))Group->SetCullDistances(7000,12000);}
            if(Piece.Roof)Group->ComponentTags.Add(TEXT("ERTemplateRoof"));
            Group->SetCanEverAffectNavigation(false);
            if(Piece.Surface)for(int32 Slot=0;Slot<FMath::Max(1,Asset->GetStaticMaterials().Num());++Slot)Group->SetMaterial(Slot,Surface);
            else if(!Piece.Mesh && Surface)Group->SetMaterial(0,Surface);
            Group->RegisterComponent();FixedMeshes.Add(Group);
        }
        FTransform T=Piece.Transform;
        if(Piece.Mesh)
        {
            const FBox Box=Asset->GetBoundingBox();const FQuat MeshRotation=Piece.MeshRotation.Quaternion(),Rotation=T.GetRotation()*MeshRotation;
            const FVector Scale=MeshRotation.UnrotateVector(T.GetScale3D()*100).GetAbs()/Box.GetSize();
            const FVector ReflectedScale=Piece.ReflectedGeometry ? Scale*FVector(1,-1,1):Scale;
            T.SetRotation(Rotation);T.SetScale3D(ReflectedScale);T.AddToTranslation(-Rotation.RotateVector(Box.GetCenter()*ReflectedScale));
            if(!Piece.VisualOnly && !Piece.PawnBarrier && !Piece.MeshCollision)FixedCollision->AddInstance(Piece.Transform);
        }
        Group->AddInstance(T);
    }
    FlushMetal();
    if(FireTraining && GetNetMode()!=NM_DedicatedServer)
        for(float X=-Layout.WarehouseWidth*.5f+600;X<Layout.WarehouseWidth*.5f;X+=1200)
            for(float Y=-Layout.WarehouseLength*.5f+600;Y<Layout.WarehouseLength*.5f;Y+=1200)
            {
                auto* Light=NewObject<UPointLightComponent>(this);AddInstanceComponent(Light);
                Light->SetupAttachment(RootComponent);Light->SetMobility(EComponentMobility::Movable);
                Light->SetRelativeLocation({X,Y,Layout.IndoorCeiling-30});Light->SetIntensity(600);Light->SetAttenuationRadius(2200);Light->SetCastShadows(false);
                Light->SetSourceRadius(40);Light->SetSpecularScale(.35f);
                Light->SetLightColor(FLinearColor::White);Light->RegisterComponent();WarehouseLights.Add(Light);
            }
    for(const auto& Desc:Layout.Ladders)
    {
        FActorSpawnParameters Params;Params.Owner=this;
        auto* Ladder=GetWorld()->SpawnActor<AERLadder>(AERLadder::StaticClass(),Desc.Frame*GetActorTransform(),Params);
        if(Ladder){Ladder->Height=Desc.Height;Ladder->DeckHeight=Desc.DeckHeight;LadderActors.Add(Ladder);}
    }
    if(ClosedRooms && GetNetMode()!=NM_DedicatedServer)
        for(const FVector P:Layout.RoomLightPoints)
        {
            auto* Light=NewObject<UPointLightComponent>(this);AddInstanceComponent(Light);
            Light->SetupAttachment(RootComponent);Light->SetMobility(EComponentMobility::Movable);
            Light->SetRelativeLocation(P);Light->SetIntensityUnits(ELightUnits::Candelas);
            Light->SetIntensity(6);Light->SetAttenuationRadius(1200);
            Light->SetCastShadows(false);Light->SetLightColor(FLinearColor(1,.94,.85));
            Light->RegisterComponent();WarehouseLights.Add(Light);
        }
    if(HasAuthority() && Layout.Routes.Num()>1 && (Template->Archetype==EERMapArchetype::CentralBuilding || Template->Archetype==EERMapArchetype::MiddleEasternAlleys || Template->Archetype==EERMapArchetype::FireTraining || Template->Archetype==EERMapArchetype::Village || ClosedRooms))
    {
        // Crossing rules have different route lengths. Never leave saved
        // placeholder points at the origin or feed bots obsolete waypoints.
        TSet<int32> Existing;
        for(TActorIterator<AERBotPatrolPoint> It(GetWorld());It;++It)
        {
            if(!Layout.Routes[1].IsValidIndex(It->RouteIndex) || Existing.Contains(It->RouteIndex)){It->Destroy();continue;}
            Existing.Add(It->RouteIndex);
            It->GetRootComponent()->SetMobility(EComponentMobility::Movable);
            It->SetActorLocation(GetActorTransform().TransformPosition(Layout.Routes[1][It->RouteIndex]));
            It->AcceptanceRadius=ClosedRooms ? 5:Layout.TownGates.IsEmpty() ? 20:10;
        }
        for(int32 I=0;I<Layout.Routes[1].Num();++I)if(!Existing.Contains(I))
        {
            auto* Point=GetWorld()->SpawnActor<AERBotPatrolPoint>(GetActorTransform().TransformPosition(Layout.Routes[1][I]),FRotator::ZeroRotator);
            Point->RouteIndex=I;Point->AcceptanceRadius=ClosedRooms ? 5:Layout.TownGates.IsEmpty() ? 20:10;
        }
    }
    else if(HasAuthority() && Layout.Routes.Num()>1)
        for(TActorIterator<AERBotPatrolPoint> It(GetWorld());It;++It)
            if(Layout.Routes[1].IsValidIndex(It->RouteIndex))
            {
                It->GetRootComponent()->SetMobility(EComponentMobility::Movable);
                It->SetActorLocation(GetActorTransform().TransformPosition(Layout.Routes[1][It->RouteIndex]));
                // Tight doglegs must be reached before turning; the ordinary
                // 90cm patrol radius would cut across a wooden leaf.
                if(!Layout.TownGates.IsEmpty())It->AcceptanceRadius=10;
            }
}
uint32 AERProceduralMap::GetTemplateGeometryDigest() const
{
    if(!LayoutConfig.TemplateAsset)return 0;
    uint32 H=GetTypeHash(LayoutConfig.TemplateAsset->Revision);
    H=HashCombine(H,GetTypeHash(LayoutConfig.ExpectedTeamSize));
    if(LayoutConfig.MapCandidate)H=HashCombine(H,GetTypeHash(LayoutConfig.MapCandidate));
    H=HashCombine(H,GetTypeHash(Layout.ThemeIndex));H=HashCombine(H,GetTypeHash(Layout.ThemeRevision));
    H=HashCombine(H,FCrc::StrCrc32(*Layout.ThemeName.ToString()));
    for(auto* M:Layout.ThemeMaterials)H=HashCombine(H,FCrc::StrCrc32(*M->GetPathName()));
    if(Layout.ThemeCeiling)H=HashCombine(H,FCrc::StrCrc32(*Layout.ThemeCeiling->GetPathName()));
    if(Layout.ThemeMetalDetails)H=HashCombine(H,FCrc::StrCrc32(*Layout.ThemeMetalDetails->GetPathName()));
    H=HashCombine(H,FCrc::StrCrc32(*LayoutConfig.TemplateAsset->TemplateId));
    H=HashCombine(H,GetTypeHash(uint8(LayoutConfig.TemplateAsset->Archetype)));
    const auto* Template=LayoutConfig.TemplateAsset.Get();
    if(auto* Module=Template->Archetype==EERMapArchetype::FireTraining ? nullptr:Template->CentralBuilding.FenceModule.Get())
    {
        H=HashCombine(H,FCrc::StrCrc32(*Module->GetPathName()));
        if(Module->GetBodySetup())H=HashCombine(H,GetTypeHash(Module->GetBodySetup()->BodySetupGuid));
    }
    if(Layout.GroundSurface)H=HashCombine(H,FCrc::StrCrc32(*Layout.GroundSurface->GetPathName()));
    auto MixVector=[&](FVector V){for(int32 I=0;I<3;++I)H=HashCombine(H,GetTypeHash(FMath::RoundToInt(V[I]*100)));};
    H=HashCombine(H,GetTypeHash(Layout.CircularArena));H=HashCombine(H,GetTypeHash(uint8(Layout.LayoutSymmetry)));
    H=HashCombine(H,GetTypeHash(Layout.CentralHill));MixVector(Layout.HillCenter);
    MixVector(FVector(Layout.HillHeight,Layout.HillPlateau,Layout.HillFalloff));
    H=HashCombine(H,GetTypeHash(Layout.CentralShape));H=HashCombine(H,GetTypeHash(uint8(Layout.CrossingRule)));
    for(const auto& Foot:Layout.CentralFootprints){MixVector(Foot.Min);MixVector(Foot.Max);}
    for(const auto& Entrance:Layout.CentralEntrances){MixVector(Entrance.GetLocation());MixVector(Entrance.GetRotation().GetForwardVector());}
    for(const auto& Cover:Layout.CoverSites)if(Cover.FiringStair!=INDEX_NONE){H=HashCombine(H,GetTypeHash(Cover.FiringStair));MixVector(Cover.FiringPosition);}
    MixVector(Layout.ArenaSize);H=HashCombine(H,GetTypeHash(bCeilings));H=HashCombine(H,GetTypeHash(uint8(LayoutConfig.TemplateAsset->Symmetry)));
    if(LayoutConfig.TemplateAsset->Archetype==EERMapArchetype::Rooms)
    {
        H=HashCombine(H,GetTypeHash(Layout.RoomsTheme));
        if(Layout.RoomsMacroVariant!=INDEX_NONE)H=HashCombine(H,GetTypeHash(Layout.RoomsMacroVariant));
        for(const auto& Screen:Layout.RoomScreens)
        {
            MixVector(Screen.Frame.GetLocation());MixVector(Screen.Frame.GetRotation().GetForwardVector());MixVector(Screen.Size);
            H=HashCombine(H,GetTypeHash(Screen.Link));H=HashCombine(H,GetTypeHash(Screen.Room));
        }
        for(const auto& Route:Layout.RoomAccessRoutes)for(const auto& Point:Route)MixVector(Point);
        for(const auto& Face:Layout.RoomShell)
        {
            MixVector(Face.A);MixVector(Face.B);MixVector(Face.C);MixVector(Face.D);
            H=HashCombine(H,GetTypeHash(Face.Roof));
            if(Face.Surface)H=HashCombine(H,FCrc::StrCrc32(*Face.Surface->GetPathName()));
        }
    }
    for(const auto& P:Layout.FixedPieces)
    {
        H=HashCombine(H,GetTypeHash(P.PawnBarrier));
        H=HashCombine(H,GetTypeHash(P.MeshCollision));
        H=HashCombine(H,GetTypeHash(P.CollisionOnly));
        if(P.PawnBarrier)H=HashCombine(H,ERIndustrialFence::GeometryVersion);
        H=HashCombine(H,GetTypeHash(P.ReflectedGeometry));
        MixVector(P.Transform.GetLocation());MixVector(P.Transform.GetScale3D());MixVector(FVector(P.MeshRotation.Pitch,P.MeshRotation.Yaw,P.MeshRotation.Roll));H=HashCombine(H,GetTypeHash(P.Roof));H=HashCombine(H,GetTypeHash(P.VisualOnly));H=HashCombine(H,GetTypeHash(uint8(P.Material)));
        MixVector(P.Transform.GetRotation().GetForwardVector());
        if(P.Surface)H=HashCombine(H,FCrc::StrCrc32(*P.Surface->GetPathName()));
        H=HashCombine(H,FCrc::StrCrc32(P.Mesh ? *P.Mesh->GetPathName():TEXT("cube")));
        if(P.Mesh && P.Mesh->GetBodySetup())H=HashCombine(H,GetTypeHash(P.Mesh->GetBodySetup()->BodySetupGuid));
    }
    for(const auto* Starts:{&Layout.AlphaSpawns,&Layout.OmegaSpawns})for(const auto& P:*Starts){MixVector(P.GetLocation());MixVector(P.GetRotation().GetForwardVector());}
    for(const auto& Zone:Layout.SpawnZones)
    {
        H=HashCombine(H,GetTypeHash(Zone.Team));H=HashCombine(H,GetTypeHash(Zone.FirstStart));H=HashCombine(H,GetTypeHash(Zone.StartCount));MixVector(Zone.Center);
        for(const auto& Point:Zone.ExitRoute)MixVector(Point);
    }
    for(const auto& Route:Layout.Routes)for(const auto& P:Route)MixVector(P);
    if(LayoutConfig.TemplateAsset->Archetype==EERMapArchetype::FireTraining)
    {
        H=HashCombine(H,GetTypeHash(Layout.WarehouseCatwalkVariant));MixVector(FVector(Layout.WarehouseWidth,Layout.WarehouseLength,Layout.IndoorCeiling));
        H=HashCombine(H,GetTypeHash(Layout.WarehouseBays));H=HashCombine(H,GetTypeHash(Layout.WarehouseBands));
        H=HashCombine(H,GetTypeHash(Layout.LayoutAttempt));H=HashCombine(H,GetTypeHash(Layout.WarehouseNaves));
        for(const auto& P:Layout.Plazas){MixVector(P.Min);MixVector(P.Max);}H=HashCombine(H,GetTypeHash(FMath::RoundToInt(Layout.WarehouseWidth)));
        for(const auto* Boxes:{&Layout.ElevatedDecks,&Layout.OverheadClosures})for(const auto& P:*Boxes){MixVector(P.Min);MixVector(P.Max);}
        for(const auto& Ladder:Layout.Ladders){MixVector(Ladder.Frame.GetLocation());MixVector(Ladder.Frame.GetRotation().GetForwardVector());MixVector(FVector(Ladder.Height,Ladder.DeckHeight,0));}
        for(const auto* Paths:{&Layout.RoomAccessRoutes,&Layout.StairRoutes,&Layout.CrossingRoutes})for(const auto& Route:*Paths)for(const auto& P:Route)MixVector(P);
    }
    for(const auto& Knot:Layout.VillageTerrainProfile)MixVector({Knot.X,Knot.Y,0});
    if(LayoutConfig.TemplateAsset->Town.Enabled)
    {
        H=HashCombine(H,GetTypeHash(Layout.GradedTerrain));
        H=HashCombine(H,GetTypeHash(Layout.SeededDistrict));
        H=HashCombine(H,GetTypeHash(Layout.DistrictAccesses));H=HashCombine(H,GetTypeHash(Layout.DistrictBends));
        H=HashCombine(H,GetTypeHash(Layout.DistrictWallCount));
        H=HashCombine(H,GetTypeHash(Layout.DistrictWallOffset));H=HashCombine(H,GetTypeHash(Layout.DistrictOpenHalfDepth));
        H=HashCombine(H,GetTypeHash(Layout.UnderpassAxis));H=HashCombine(H,GetTypeHash(Layout.CentralLowCovers));
        for(const auto& R:Layout.UnderpassRamps){MixVector(R.Bounds.Min);MixVector(R.Bounds.Max);MixVector(R.Entry);MixVector(R.Exit);}
        for(const auto* Bounds:{&Layout.UnderpassBreaches,&Layout.LowCovers})for(const auto& P:*Bounds){MixVector(P.Min);MixVector(P.Max);}
        for(const auto& P:Layout.UnderpassRoute)MixVector(P);
        for(const auto& Tower:Layout.DistrictTowers){MixVector(Tower.GetLocation());MixVector(Tower.GetRotation().GetForwardVector());}
        for(float Height:Layout.DistrictTowerHeights)H=HashCombine(H,GetTypeHash(Height));
        for(const auto& Route:Layout.DistrictRoutes)for(const auto& P:Route)MixVector(P);
        for(const auto& P:Layout.TerrainPads){MixVector(P.Min);MixVector(P.Max);}
        for(const auto* Routes:{&Layout.RooftopRoutes,&Layout.GroundRoutes})for(const auto& Route:*Routes)for(const auto& P:Route)MixVector(P);
        for(const auto& House:Layout.Houses)
        {
            H=HashCombine(H,FCrc::StrCrc32(*House.StructureName.ToString()));
            H=HashCombine(H,GetTypeHash(House.RimHeight));
            for(const auto& Mouth:House.RoofMouths)MixVector(Mouth);
            for(const auto& Route:House.Entrances)for(const auto& P:Route)MixVector(P);
        }
        for(const auto& Route:Layout.RoofTransfers)for(const auto& P:Route)MixVector(P);
        MixVector(Layout.TownCrossing);
        for(const auto* Paths:{&Layout.RoomAccessRoutes,&Layout.StairRoutes})for(const auto& Route:*Paths)for(const auto& P:Route)MixVector(P);
        for(const auto& P:Layout.UpperRoute)MixVector(P);
        for(const auto& G:Layout.TownGates){MixVector(G.Frame.GetLocation());MixVector(FVector(G.Gap,G.Width,G.Angle));for(const auto& P:G.Passage)MixVector(P);}
    }
    return H;
}
