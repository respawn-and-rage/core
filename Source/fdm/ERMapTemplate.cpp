#include "ERMapTemplate.h"
#include "ERProceduralMap.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "ERMeshCells.h"

namespace ERProcedural
{
bool BuildTemplate(const FERProceduralConfig& C,const FERBlockRoomConfig& B,FERProceduralLayout& Out,const FERCellSettings& Cells)
{
    const auto* T=C.TemplateAsset.Get();
    if(!T || T->TemplateId.IsEmpty() || T->Revision<1 || C.Floors!=1 || B.BlockSize!=FVector(50,20,25) ||
        !FMath::IsFinite(B.ChunkSize) || B.ChunkSize<100 || T->Pieces.Num()>256 ||
        T->ArenaSize.ContainsNaN() || T->ArenaSize.X<2000 || T->ArenaSize.Y<3000 || T->ArenaSize.Z<400 ||
        T->ArenaSize.X>10000 || T->ArenaSize.Y>16000 || T->ArenaSize.Z>1600 || uint8(T->Symmetry)>uint8(EERTemplateSymmetry::Mirror) ||
        uint8(T->Archetype)>uint8(EERMapArchetype::FireTraining)) return false;
    const bool Central=T->Archetype==EERMapArchetype::CentralBuilding;
    const bool Alleys=T->Archetype==EERMapArchetype::MiddleEasternAlleys;
    const bool Fire=T->Archetype==EERMapArchetype::FireTraining;
    if(Alleys && (!T->Town.Enabled || T->Variation.Enabled))return false;
    if(Fire && (T->Town.Enabled || T->Variation.Enabled || T->RoofMesh || T->ShellWallMesh || C.LayoutAttempt<0 || C.LayoutAttempt>=FireTrainingAttempts))return false;
    if(!Central && !Alleys && !Fire && (T->Pieces.IsEmpty() || T->AlphaSpawns.IsEmpty() || T->AlphaSpawns.Num()>32 || T->OmegaSpawns.Num()>32 || T->PatrolRoute.Num()<2 || T->PatrolRoute.Num()>128))return false;

    auto ValidMesh=[](UStaticMesh* M){return !M || (!M->GetBoundingBox().GetSize().ContainsNaN() && M->GetBoundingBox().GetSize().GetMin()>.1);};
    if(!ValidMesh(T->ShellWallMesh) || !ValidMesh(T->RoofMesh))return false;
    FERProceduralLayout L;L.ArenaSize=T->ArenaSize;L.AlphaSpawns=T->AlphaSpawns;L.OmegaSpawns=T->OmegaSpawns;
    TArray<FERMapTemplatePiece> Pieces=T->Pieces;TArray<FVector> Patrol=T->PatrolRoute;
    if(C.CrossingOverride<0 || C.CrossingOverride>3)return false;
    if(Central)
    {
        if(T->Town.Enabled || T->Variation.Enabled)return false;
        // Recipe overrides live in the replicated descriptor, never mutate the
        // shared data asset (another world may use it in the same process).
        if(!BuildCentralBuilding(*T,C.Seed,Pieces,L,Patrol,C.CrossingOverride))return false;
    }
    if(Fire && !BuildFireTraining(*T,C.Seed,C.LayoutAttempt,Pieces,L,Patrol))return false;
    if(T->Town.Enabled && (T->Variation.Enabled || !BuildDustTown(*T,C.Seed,Pieces,L,Patrol)))return false;
    if(T->Variation.Enabled && !BuildTemplateVariants(*T,C.Seed,Pieces,L,Patrol))return false;
    const bool BreakArchitecture=Alleys && T->Town.DestructibleArchitecture;
    if(BreakArchitecture && !T->Town.ProceduralHouses)for(const auto& Mesh:T->Town.BuildingMeshes)
        if(!T->Town.BuildingStructures.ContainsByPredicate([&](const auto& S){return S && S->Source==Mesh && S->GeometryCRC && S->Corners.Num()%3==0;}))return false;
    if(L.LayoutSymmetry==EERTemplateSymmetry::None)L.LayoutSymmetry=T->Symmetry;
    const bool Mirror=L.LayoutSymmetry==EERTemplateSymmetry::Mirror;
    const bool Symmetric=L.LayoutSymmetry!=EERTemplateSymmetry::None;
    const FVector Size=L.ArenaSize,Half=Size*.5;
    L.Corridors.Add({FVector2D(-Half.X,-Half.Y),FVector2D(Half.X,Half.Y)});
    TArray<FBox> Volumes;TArray<FTransform> VolumeFrames;int32 Wall=0;
    for(const auto& Piece:Pieces)
    {
        const float MinDimension=Piece.Destructible && Piece.Material==EERBlockMaterial::Metal ? Cells.SheetThickness:1.f;
        if(Piece.Size.GetMin()<MinDimension-.001f)UE_LOG(LogTemp,Warning,TEXT("ER_TEMPLATE_PIECE_REJECT seed=%d size=%s"),C.Seed,*Piece.Size.ToString());
        if(Piece.Center.ContainsNaN() || Piece.Size.ContainsNaN() || Piece.Jitter.ContainsNaN() || Piece.MeshRotation.ContainsNaN() || !FMath::IsFinite(Piece.RotationYaw) ||
            Piece.Size.GetMin()<MinDimension-.001f || Piece.Size.GetMax()>16000 || Piece.Jitter.X<0 || Piece.Jitter.Y<0 ||
            Piece.Jitter.X>200 || Piece.Jitter.Y>200 || uint8(Piece.Material)>6 || (Piece.Destructible && Piece.FixedMesh) || !ValidMesh(Piece.FixedMesh) ||
            !FMath::IsFinite(Piece.Strength) || Piece.Strength<1 || Piece.Strength>16 ||
            (Piece.WalkableSurface && (Piece.FixedMesh || Piece.VisualOnly)) ||
            (Piece.PawnBarrier && (Piece.Destructible || Piece.VisualOnly || Piece.WalkableSurface)) ||
            (Piece.MeshCollision && (!Piece.FixedMesh || Piece.Destructible || Piece.VisualOnly || Piece.PawnBarrier || Piece.WalkableSurface)) ||
            (Piece.CollisionOnly && (Piece.Destructible || Piece.FixedMesh || Piece.VisualOnly || Piece.PawnBarrier || Piece.WalkableSurface || Piece.MeshCollision)) ||
            (Piece.Roof && (Piece.Destructible || !Piece.FixedMesh)) ||
            (Piece.Destructible && (Piece.VisualOnly || Piece.FixedSurface)) || (!Piece.Destructible && (Piece.CellSurface || Piece.MirroredCellSurface))) return false;
        if(Piece.SymmetryCenter.ContainsNaN() || (!Central && !Piece.SymmetryCenter.IsNearlyZero()) || Piece.SymmetryCenter.Y!=0 || Piece.SymmetryCenter.Z!=0 || FMath::Abs(Piece.SymmetryCenter.X)>1000)return false;
        if(Symmetric && !Piece.Mirrored && ((!Mirror && !FMath::IsNearlyEqual(Piece.Center.X,Piece.SymmetryCenter.X)) || !FMath::IsNearlyZero(Piece.Center.Y) || !Piece.Jitter.IsNearlyZero()))return false;
        FRandomStream Random(int32(HashCombine(uint32(C.Seed),uint32(Piece.Group))));
        const float OffsetX=Random.RandRange(-int32(Piece.Jitter.X/50),int32(Piece.Jitter.X/50))*50;
        const float OffsetY=Random.RandRange(-int32(Piece.Jitter.Y/50),int32(Piece.Jitter.Y/50))*50;
        const FVector Original=Piece.Center+FVector(OffsetX,OffsetY,0);
        for(int32 Copy=0;Copy<(Symmetric && Piece.Mirrored ? 2:1);++Copy)
        {
            const FVector Center=Copy ? FVector(Mirror ? Original.X:2*Piece.SymmetryCenter.X-Original.X,-Original.Y,Original.Z):Original;
            const FQuat PhysicalRotation=FRotator(0,Copy ? (Mirror ? -Piece.RotationYaw:Piece.RotationYaw+180):Piece.RotationYaw,0).Quaternion();
            const FBox Bounds=FBox(-Piece.Size*.5,Piece.Size*.5).TransformBy(FTransform(Piece.RotationYaw!=0 ? PhysicalRotation:FQuat::Identity,Center));
            const FVector Min=Bounds.Min,Max=Bounds.Max;
            const float FloorLimit=Alleys && T->Town.LowCoverAndUnderpass ? -420.f:0.f;
            if(Min.X < -Half.X-.01 || Max.X > Half.X+.01 || Min.Y < -Half.Y-.01 || Max.Y > Half.Y+.01 || Min.Z<FloorLimit-.01 || Max.Z>Size.Z+.01)
            {UE_LOG(LogTemp,Warning,TEXT("ER_TEMPLATE_BOUNDS_REJECT seed=%d bounds=%s arena=%s"),C.Seed,*Bounds.ToString(),*Size.ToString());return false;}
            const FBox Box(Min,Max);
            const FTransform Frame(Piece.RotationYaw!=0 ? PhysicalRotation:FQuat::Identity,Center,Piece.Size);
            if(!Piece.VisualOnly && !Piece.MeshCollision)for(int32 VI=0;VI<Volumes.Num();++VI)if(Box.Intersect(Volumes[VI]) && Box.Overlap(Volumes[VI]).GetSize().GetMin()>.01)
            {
                const auto& Other=VolumeFrames[VI];bool Separated=false;
                // Exact planar SAT for the angled door planks. Their
                // conservative world AABBs overlap even when faces just touch.
                for(const FVector Axis:{Frame.GetRotation().GetAxisX(),Frame.GetRotation().GetAxisY(),Other.GetRotation().GetAxisX(),Other.GetRotation().GetAxisY()})
                {
                    auto Radius=[&](const FTransform& F){return .5*(F.GetScale3D().X*FMath::Abs(FVector::DotProduct(F.GetRotation().GetAxisX(),Axis))+F.GetScale3D().Y*FMath::Abs(FVector::DotProduct(F.GetRotation().GetAxisY(),Axis)));};
                    if(FMath::Abs(FVector::DotProduct(Center-Other.GetLocation(),Axis))>=Radius(Frame)+Radius(Other)-.01){Separated=true;break;}
                }
                if(!Separated){UE_LOG(LogTemp,Warning,TEXT("ER_TEMPLATE_REJECT seed=%d overlapping volume %s with %s"),C.Seed,*Box.ToString(),*Volumes[VI].ToString());return false;}
            }
            // A hollow mesh's bounds are not a solid: doors, rooms and roof
            // access receive exact triangle/capsule checks in physical tests.
            if(!Piece.VisualOnly && !Piece.MeshCollision){Volumes.Add(Box);VolumeFrames.Add(Frame);}
            if(!Piece.Destructible)
            {
                FRotator Rotation=Piece.MeshRotation;if(Copy){if(Mirror){Rotation.Yaw=-Rotation.Yaw;Rotation.Roll=-Rotation.Roll;}else if(Piece.RotationYaw==0)Rotation.Yaw+=180;}
                // Legacy axis-aligned paired pieces keep their old physical
                // transform; only explicit town leaves rotate their boxes.
                const FQuat R=Piece.RotationYaw!=0 ? PhysicalRotation:FQuat::Identity;
                if(BreakArchitecture && Piece.MeshCollision && T->Town.BuildingMeshes.Contains(Piece.FixedMesh))continue;
                const UERDestructibleMesh* Structure=nullptr;
                if(BreakArchitecture && Piece.FixedMesh)for(const auto& S:T->Town.BuildingStructures)if(S && S->Source==Piece.FixedMesh){Structure=S;break;}
                const bool Masonry=BreakArchitecture && !Piece.FixedMesh && !Piece.VisualOnly &&
                    (Piece.FixedSurface==T->Town.WallSurface || (Piece.FixedSurface==T->Town.RoofSurface && Piece.Size.Z<=30));
                if(Masonry || Structure)
                {
                    auto& P=L.Panels.AddDefaulted_GetRef();P.Id=Wall++*8;P.Material=EERBlockMaterial::Stone;
                    P.Size=Structure ? Rotation.UnrotateVector(Piece.Size).GetAbs():Piece.Size;
                    const FQuat Q=R*(Structure ? Rotation.Quaternion():FQuat::Identity);
                    P.Transform=FTransform(Q,Center-Q.RotateVector(P.Size*.5));
                    P.Cell=FVector(FERCellWorld::FitCell(P.Size.X,Cells.CellSize),FERCellWorld::FitCell(P.Size.Y,Cells.CellSize),FERCellWorld::FitCell(P.Size.Z,Cells.CellSize));
                    // Authored town foundations are at 0 or +180cm, as for the imported shells.
                    P.bAnchorBottom=P.Transform.GetLocation().Z<=180.1;
                    P.Surface=Structure ? Structure->Source->GetMaterial(0):Piece.FixedSurface.Get();
                    if(Structure){P.Skin=Structure->Corners;P.SkinCRC=Structure->GeometryCRC;P.SkinName=Structure->Source->GetFName();for(auto& V:P.Skin){V.Position*=P.Size;V.Normal=(V.Normal*Structure->Source->GetBoundingBox().GetSize()/P.Size).GetSafeNormal();}}
                    else ERMeshCells::BoxSkin(P);
                    continue;
                }
                L.FixedPieces.Add({FTransform(R,Center,Piece.Size/100),Piece.Material,Piece.FixedMesh,Rotation,Piece.Roof,Piece.VisualOnly,Piece.FixedSurface,Piece.PawnBarrier,Piece.MeshCollision,Piece.CollisionOnly,Copy!=0 && Mirror});continue;
            }
            // Generate in an orthonormal local grid, then rotate the entire
            // construction. Its cells/collision/support never use a world AABB.
            const FTransform Placement(PhysicalRotation,Center);
            const bool Ground=Center.Z-Piece.Size.Z*.5<=(L.CircularArena ? CentralGroundHeight(L,FVector2D(Center))+.01:.01);
            const int32 First=L.Panels.Num();
            if(Piece.WalkableSurface)
            {
                const bool Wood=Piece.Material==EERBlockMaterial::Wood,Metal=Piece.Material==EERBlockMaterial::Metal;
                if((Wood && !FMath::IsNearlyEqual(Piece.Size.Z,Cells.WoodDepth)) || (Metal && !FMath::IsNearlyEqual(Piece.Size.Z,Cells.SheetThickness)))return false;
                auto& P=L.Panels.AddDefaulted_GetRef();P.Id=Wall*8;P.Material=Piece.Material;
                P.Transform=FTransform(FMatrix(FVector(1,0,0),FVector(0,0,-1),FVector(0,1,0),FVector(-Piece.Size.X*.5,-Piece.Size.Y*.5,Piece.Size.Z*.5)))*Placement;
                P.Size=FVector(Piece.Size.X,Piece.Size.Z,Piece.Size.Y);
                P.Cell=FVector(FERCellWorld::FitCell(P.Size.X,Cells.CellSize),Wood || Metal ? P.Size.Y:FERCellWorld::FitCell(P.Size.Y,Cells.CellSize),FERCellWorld::FitCell(P.Size.Z,Cells.CellSize));
                P.bAnchored=Ground || Piece.Anchored;
            }
            else
            {
                AddPieceCells(Wall,Piece.Material,FBox(FVector::ZeroVector,Piece.Size),false,Cells,L.Panels);
                const FTransform Origin=FTransform(-Piece.Size*.5)*Placement;
                for(int32 P=First;P<L.Panels.Num();++P)
                {L.Panels[P].Transform=L.Panels[P].Transform*Origin;L.Panels[P].bAnchorBottom&=Ground || Piece.Anchored;}
            }
            for(int32 P=First;P<L.Panels.Num();++P){L.Panels[P].Strength=Piece.Strength;L.Panels[P].Surface=Copy && Piece.MirroredCellSurface ? Piece.MirroredCellSurface.Get():Piece.CellSurface.Get();}
            ++Wall;
        }
    }
    if(BreakArchitecture)for(const auto& House:L.Houses)
    {
        if(!House.StructureName.IsNone())continue; // already generated as one module skin
        const UERDestructibleMesh* Source=nullptr;for(const auto& S:T->Town.BuildingStructures)if(S && S->Source==House.Mesh){Source=S;break;}
        if(!Source)return false;
        auto& P=L.Panels.AddDefaulted_GetRef();P.Id=Wall++*8;
        P.Material=House.Mesh->GetName().Contains(TEXT("Concrete")) ? EERBlockMaterial::Concrete:EERBlockMaterial::Brick;
        P.Size=House.Frame.GetScale3D();
        P.Transform=FTransform(House.Frame.GetRotation(),House.Frame.GetLocation()-House.Frame.GetRotation().RotateVector(FVector(P.Size.X*.5,P.Size.Y*.5,0)));
        P.Cell={FERCellWorld::FitCell(P.Size.X,Cells.CellSize),FERCellWorld::FitCell(P.Size.Y,Cells.CellSize),FERCellWorld::FitCell(P.Size.Z,Cells.CellSize)};
        P.bAnchorBottom=true;P.Surface=House.Mesh->GetMaterial(0);P.SkinCRC=Source->GeometryCRC;P.SkinName=House.Mesh->GetFName();
        P.Skin=Source->Corners;for(auto& V:P.Skin){V.Position*=P.Size;V.Normal=(V.Normal*Source->Source->GetBoundingBox().GetSize()/P.Size).GetSafeNormal();}
    }
    // Fixed shed panels tile at <=5m to retain vendor detail and texel scale.
    auto Fixed=[&](FVector Center,FVector Size,FRotator Rotation=FRotator::ZeroRotator){L.FixedPieces.Add({FTransform(FQuat::Identity,Center,Size/100),Alleys ? EERBlockMaterial::Stone:EERBlockMaterial::Metal,T->ShellWallMesh,Rotation,false,false,Alleys ? T->Town.WallSurface.Get():nullptr});};
    if(Central || Fire) { /* The archetype owns its fence perimeter or warehouse envelope. */ }
    else if(T->ShellWallMesh)
    {
        const int32 Rows=FMath::CeilToInt(Size.Z/525);
        for(int32 Row=0;Row<Rows;++Row)
        {
            const float H=Size.Z/Rows,Z=(Row+.5)*H;
            for(int32 Axis=0;Axis<2;++Axis)
            {
                const float Length=Axis ? Size.X:Size.Y;
                const int32 Count=FMath::CeilToInt(Length/500);const float Width=Length/Count;
                for(int32 I=0;I<Count;++I)for(float Sign:{-1.f,1.f})
                {
                    const float Along=(I-(Count-1)*.5)*Width;
                    Fixed(Axis ? FVector(Along,Sign*(Half.Y+10),Z):FVector(Sign*(Half.X+10),Along,Z),Axis ? FVector(Width,20,H):FVector(20,Width,H),FRotator(0,(T->ShellWallMesh->GetBoundingBox().GetSize().X<T->ShellWallMesh->GetBoundingBox().GetSize().Y) ? (Axis ? 90:0):(Axis ? 0:90),0));
                }
            }
        }
    }
    else
    {
        // Reachable roofs must not turn the perimeter into a mantle exit.
        const float ShellHeight=Alleys && T->Town.TraversableTown ? FMath::Max(float(Size.Z),T->Town.BuildingHeight.Y+600):float(Size.Z);
        Fixed(FVector(-Half.X-10,0,ShellHeight*.5),FVector(20,Size.Y+40,ShellHeight));
        Fixed(FVector(Half.X+10,0,ShellHeight*.5),FVector(20,Size.Y+40,ShellHeight));
        for(float Sign:{-1.f,1.f})Fixed(FVector(0,Sign*(Half.Y+10),ShellHeight*.5),FVector(Size.X,20,ShellHeight));
    }
    if(T->RoofMesh)
    {
        const int32 NX=FMath::CeilToInt(Size.X/500),NY=FMath::CeilToInt(Size.Y/500);
        // Tiles reach the outer faces of the 20cm shell walls, like the collision ceiling:
        // a roof that only touches the wall top along an edge leaks a sunlight line.
        const FVector Cell((Size.X+40)/NX,(Size.Y+40)/NY,20);
        for(int32 X=0;X<NX;++X)for(int32 Y=0;Y<NY;++Y)L.FixedPieces.Add({FTransform(FQuat::Identity,FVector((X-(NX-1)*.5)*Cell.X,(Y-(NY-1)*.5)*Cell.Y,Size.Z+10),Cell/100),EERBlockMaterial::Metal,T->RoofMesh,(T->RoofMesh->GetBoundingBox().GetSize().X<T->RoofMesh->GetBoundingBox().GetSize().Y ? FRotator(90,0,0):FRotator(0,0,90)),true,true});
    }
    auto Clear=[&](FVector P)
    {
        if(L.CircularArena && FVector2D(P).Size()>Half.X-50)return false;
        if(P.ContainsNaN() || FMath::Abs(P.X)>Half.X-40 || FMath::Abs(P.Y)>Half.Y-40 || P.Z<94 || P.Z>Size.Z-94)return false;
        for(const auto& Frame:VolumeFrames)
        {
            const FVector HalfSize=Frame.GetScale3D()*.5;
            if(FBox(-HalfSize,HalfSize).ExpandBy(FVector(35,35,93)).IsInsideOrOn(Frame.InverseTransformPositionNoScale(P)))return false;
        }
        return true;
    };
    if(Symmetric){L.OmegaSpawns.Reset();for(const auto& S:L.AlphaSpawns){auto P=S.GetLocation();if(!Mirror)P.X=-P.X;P.Y=-P.Y;L.OmegaSpawns.Add(FTransform(FRotator(0,Mirror ? -S.Rotator().Yaw:S.Rotator().Yaw+180,0),P));}}
    if(L.OmegaSpawns.IsEmpty())return false;
    for(const auto* Starts:{&L.AlphaSpawns,&L.OmegaSpawns})for(const auto& Start:*Starts)if(Start.ContainsNaN() || !Clear(Start.GetLocation()))
    {UE_LOG(LogTemp,Warning,TEXT("ER_TEMPLATE_SPAWN_REJECT seed=%d point=%s"),C.Seed,*Start.GetLocation().ToString());return false;}
    for(const auto& P:Patrol)if(!Clear(P)){UE_LOG(LogTemp,Warning,TEXT("ER_TEMPLATE_PATROL_REJECT seed=%d point=%s"),C.Seed,*P.ToString());return false;}
    // Validate complete circulation/room paths, not just their endpoints.
    auto ClearPath=[&](const TArray<FVector>& Path,bool Closed)
    {
        for(int32 I=0;I<(Closed ? Path.Num():Path.Num()-1);++I)
        {
            const FVector A=Path[I],B=Path[(I+1)%Path.Num()],Delta=B-A;
            // Test in each solid's unscaled local frame. World AABBs would
            // incorrectly seal the diagonal slot between opposite-swing leaves.
            for(const auto& Frame:VolumeFrames)
            {
                const FVector HalfSize=Frame.GetScale3D()*.5;
                const FBox Box=FBox(-HalfSize,HalfSize).ExpandBy(FVector(35,35,93));
                const FVector LocalA=Frame.InverseTransformPositionNoScale(A),LocalB=Frame.InverseTransformPositionNoScale(B);
                if(FMath::LineBoxIntersection(Box,LocalA,LocalB,LocalB-LocalA))
                {UE_LOG(LogTemp,Warning,TEXT("ER_TEMPLATE_ROUTE_REJECT seed=%d from=%s to=%s volume=%s size=%s"),C.Seed,*A.ToString(),*B.ToString(),*Frame.GetLocation().ToString(),*Frame.GetScale3D().ToString());return false;}
            }
        }
        return true;
    };
    if(!ClearPath(Patrol,true))return false;
    for(const auto& Route:L.RoomAccessRoutes)if(!ClearPath(Route,false))return false;
    for(const auto& Route:L.CrossingRoutes)if(!ClearPath(Route,false))return false;
    L.Spawn=L.AlphaSpawns[0].GetLocation();L.Routes.Add(L.SpawnExitRoutes.Num()==2 ? L.SpawnExitRoutes[0]:TArray<FVector>{L.Spawn,Patrol[0]});L.Routes.Add(Patrol);L.Routes.Add(L.SpawnExitRoutes.Num()==2 ? L.SpawnExitRoutes[1]:TArray<FVector>{L.OmegaSpawns[0].GetLocation(),Patrol.Last()});
    if(L.Panels.IsEmpty() || L.FixedPieces.Num()>(Fire ? 8192:2048))return false;
    Out=MoveTemp(L);return true;
}
}

bool UERMapTemplate::ValidateLayout(int32 Seed) const
{
    FERProceduralConfig C;C.Seed=Seed;C.TemplateAsset=const_cast<UERMapTemplate*>(this);
    FERBlockRoomConfig B;FERProceduralLayout L;return ERProcedural::Build(C,B,L);
}

bool UERMapTemplate::PrepareWarehouseCollision(UStaticMesh* Mesh)
{
#if WITH_EDITOR
    if(!Mesh || !Mesh->GetPathName().StartsWith(TEXT("/Game/EclipseRecoil/Art/FireTraining/Meshes/SM_ER_WH_")))return false;
    auto* Body=Mesh->GetBodySetup();if(!Body)return false;
    Mesh->Modify();Body->Modify();Body->CollisionTraceFlag=CTF_UseComplexAsSimple;Body->bDoubleSidedGeometry=true;
    Body->InvalidatePhysicsData();Body->CreatePhysicsMeshes();Mesh->MarkPackageDirty();return true;
#else
    return false;
#endif
}

bool UERMapTemplate::PrepareAlleyHouseCollision(UStaticMesh* Mesh)
{
#if WITH_EDITOR
    if(!Mesh || !Mesh->GetPathName().StartsWith(TEXT("/Game/EclipseRecoil/Art/MiddleEasternAlleys/Meshes/SM_ER_")) ||
       (!Mesh->GetName().Contains(TEXT("ConcreteHouse")) && !Mesh->GetName().Contains(TEXT("DesertHouse"))))return false;
    auto* Body=Mesh->GetBodySetup();if(!Body)return false;
    Mesh->Modify();Body->Modify();Body->CollisionTraceFlag=CTF_UseComplexAsSimple;Body->bDoubleSidedGeometry=true;
    Body->InvalidatePhysicsData();Body->CreatePhysicsMeshes();Mesh->MarkPackageDirty();return true;
#else
    return false;
#endif
}
