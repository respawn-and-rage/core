#include "ERProceduralMap.h"
#include "Algo/Reverse.h"

namespace ERProcedural
{
namespace
{
FVector Reflect(FVector P){P.Y=-P.Y;return P;}
FTransform ReflectFrame(const FTransform& F)
{
    const auto Q=F.GetRotation();
    const FMatrix Axes(Reflect(Q.GetAxisX()),-Reflect(Q.GetAxisY()),Reflect(Q.GetAxisZ()),FVector::ZeroVector);
    return FTransform(FQuat(Axes),Reflect(F.GetLocation()),F.GetScale3D());
}
FBox ReflectBox(const FBox& B){return FBox(FVector(B.Min.X,-B.Max.Y,B.Min.Z),FVector(B.Max.X,-B.Min.Y,B.Max.Z));}
// A rectangle that crosses the reflection plane may only be cut along a local
// grid axis. Oblique seams fail closed and participate in deterministic retries.
bool ClipFrame(FTransform& Frame,FVector& Size,bool Corner,FVector& Offset)
{
    const auto Q=Frame.GetRotation();const FVector Origin=Corner ? Frame.GetLocation():Frame.GetLocation()-Q.RotateVector(Size*.5);
    const FBox Bounds=FBox(FVector::ZeroVector,Size).TransformBy(FTransform(Q,Origin));
    Offset=FVector::ZeroVector;
    if(Bounds.Max.Y<=.001)return true;
    int32 Axis=INDEX_NONE;double Direction=0;
    for(int32 I=0;I<3;++I)
    {const double Y=Q.RotateVector(FVector(I==0,I==1,I==2)).Y;if(FMath::Abs(Y)>.9999){Axis=I;Direction=Y;}}
    if(Axis==INDEX_NONE)return false;
    const double Cut=-Origin.Y/Direction;
    const double Lo=Direction>0 ? 0:FMath::Clamp(Cut,0.,Size[Axis]),Hi=Direction>0 ? FMath::Clamp(Cut,0.,Size[Axis]):Size[Axis];
    if(Hi-Lo<.001)return false;
    Offset[Axis]=Lo;Size[Axis]=Hi-Lo;
    Frame.SetLocation(Origin+Q.RotateVector(Offset+(Corner ? FVector::ZeroVector:Size*.5)));
    return true;
}
void ClipSkin(FERCellPanelDesc& P,FVector Offset)
{
    if(P.Skin.IsEmpty())return;
    TArray<FERMeshCorner> Result;
    for(int32 Triangle=0;Triangle<P.Skin.Num();Triangle+=3)
    {
        TArray<FERMeshCorner> Polygon{P.Skin[Triangle],P.Skin[Triangle+1],P.Skin[Triangle+2]};
        for(int32 Axis=0;Axis<3 && !Polygon.IsEmpty();++Axis)for(int32 Side=0;Side<2;++Side)
        {
            TArray<FERMeshCorner> Next;
            const double Limit=Offset[Axis]+(Side ? P.Size[Axis]:0);
            auto Distance=[&](const FERMeshCorner& V){return (V.Position[Axis]-Limit)*(Side ? -1:1);};
            for(int32 I=0;I<Polygon.Num();++I)
            {
                const auto& A=Polygon[I];const auto& B=Polygon[(I+1)%Polygon.Num()];
                const double DA=Distance(A),DB=Distance(B);
                if(DA>=0)Next.Add(A);
                if((DA<0)!=(DB<0))
                {
                    const double T=DA/(DA-DB);FERMeshCorner V;
                    V.Position=FMath::Lerp(A.Position,B.Position,T);V.Normal=FMath::Lerp(A.Normal,B.Normal,T).GetSafeNormal();V.UV=FMath::Lerp(A.UV,B.UV,T);Next.Add(V);
                }
            }
            Polygon=MoveTemp(Next);
        }
        for(int32 I=1;I+1<Polygon.Num();++I)for(int32 J:{0,I,I+1}){auto V=Polygon[J];V.Position-=Offset;Result.Add(V);}
    }
    P.Skin=MoveTemp(Result);
}
FERCellPanelDesc ReflectPanel(FERCellPanelDesc P)
{
    // Reflect local X too, retaining an orthonormal, positive-scale cell grid.
    const auto Q=P.Transform.GetRotation();
    const FMatrix Axes(-Reflect(Q.GetAxisX()),Reflect(Q.GetAxisY()),Reflect(Q.GetAxisZ()),FVector::ZeroVector);
    P.Transform=FTransform(FQuat(Axes),Reflect(P.Transform.TransformPosition({P.Size.X,0,0})));
    for(auto& H:P.Holes){const double Min=P.Size.X-H.Max.X;H.Max.X=P.Size.X-H.Min.X;H.Min.X=Min;}
    for(auto& V:P.Skin){V.Position.X=P.Size.X-V.Position.X;V.Normal.X=-V.Normal.X;}
    for(int32 I=0;I<P.Skin.Num();I+=3)Swap(P.Skin[I+1],P.Skin[I+2]);
    return P;
}
TArray<FVector> ClipPolygon(const TArray<FVector>& Polygon)
{
    TArray<FVector> Result;
    for(int32 I=0;I<Polygon.Num();++I)
    {
        const auto A=Polygon[I],B=Polygon[(I+1)%Polygon.Num()];
        if(A.Y<=0)Result.Add(A);
        if((A.Y<0)!=(B.Y<0) && FMath::Abs(B.Y-A.Y)>.001)Result.Add(FMath::Lerp(A,B,-A.Y/(B.Y-A.Y)));
    }
    // Exact seam vertices can appear twice after clipping.
    for(int32 I=Result.Num()-1;I>=0 && Result.Num()>1;--I)if(Result[I].Equals(Result[(I+1)%Result.Num()],.001))Result.RemoveAt(I);
    return Result;
}
void FoldPaths(TArray<TArray<FVector>>& Paths)
{
    TArray<TArray<FVector>> Result;
    for(const auto& Path:Paths)
    {
        TArray<FVector> Part;
        auto Flush=[&]()
        {
            if(Part.Num()>1){Result.Add(Part);for(auto& P:Part)P=Reflect(P);Result.Add(Part);}Part.Reset();
        };
        for(int32 I=0;I<Path.Num();++I)
        {
            if(I>0 && ((Path[I-1].Y<0)!=(Path[I].Y<0)))
            {
                const auto P=FMath::Lerp(Path[I-1],Path[I],-Path[I-1].Y/(Path[I].Y-Path[I-1].Y));
                Part.Add(P);if(Path[I].Y>0)Flush();
            }
            if(Path[I].Y<=0)Part.Add(Path[I]);
        }
        Flush();
    }
    Paths=MoveTemp(Result);
}
}
bool FoldLayoutToMirror(FERProceduralLayout& L,const FERCellSettings& Cells)
{
    TArray<FERCellPanelDesc> Panels;
    for(const auto& Original:L.Panels)
    {
        const FBox Bounds=FBox(FVector::ZeroVector,Original.Size).TransformBy(Original.Transform);
        if(Bounds.Min.Y>=-.001)continue;
        if(FMath::Abs(Bounds.Min.Y+Bounds.Max.Y)<.001 && FMath::Abs(Original.Transform.GetRotation().GetAxisY().Y)>.9999 &&
           Original.Size.Y<=Cells.WoodDepth+.001 && Original.Skin.IsEmpty())
        {auto P=Original;P.Id=Panels.Num();Panels.Add(P);continue;}
        auto P=Original;FVector Offset;
        if(!ClipFrame(P.Transform,P.Size,true,Offset)){UE_LOG(LogTemp,Warning,TEXT("ER_MIRROR_PANEL_SEAM id=%d bounds=%s"),P.Id,*Bounds.ToString());return false;}
        if(P.Size!=Original.Size)
        {
            ClipSkin(P,Offset);
            for(auto& H:P.Holes){H.Min-=FVector2D(Offset.X,Offset.Z);H.Max-=FVector2D(Offset.X,Offset.Z);}
            for(int32 Axis=0;Axis<3;++Axis)if(P.Size[Axis]!=Original.Size[Axis])P.Cell[Axis]=FERCellWorld::FitCell(P.Size[Axis],Cells.CellSize);
        }
        P.Id=Panels.Num();Panels.Add(P);P=ReflectPanel(P);P.Id=Panels.Num();Panels.Add(MoveTemp(P));
    }
    L.Panels=MoveTemp(Panels);
    TArray<FERLayoutFixedPiece> Fixed;
    for(auto P:L.FixedPieces)
    {
        FVector Size=P.Transform.GetScale3D()*100,Offset;const auto Q=P.Transform.GetRotation();
        const auto Bounds=FBox(-Size*.5,Size*.5).TransformBy(FTransform(Q,P.Transform.GetLocation()));
        if(Bounds.Min.Y>=-.001)continue;
        // Imported members are indivisible. Their union with the reflected
        // member preserves both the exact original mesh and reflection symmetry.
        if(P.Mesh)
        {
            if(P.Transform.GetLocation().Y>0)continue;
            Fixed.Add(P);P.Transform=ReflectFrame(P.Transform);P.ReflectedGeometry=!P.ReflectedGeometry;
            P.MeshRotation=FRotator(P.MeshRotation.Pitch,-P.MeshRotation.Yaw,-P.MeshRotation.Roll);Fixed.Add(P);continue;
        }
        if(!ClipFrame(P.Transform,Size,false,Offset))return false;

        P.Transform.SetScale3D(Size/100);Fixed.Add(P);P.Transform=ReflectFrame(P.Transform);
        // Imported geometry is reflected, including its collision triangles.
        // Box-only fixed geometry needs no negative scale.
        if(P.Mesh)P.ReflectedGeometry=!P.ReflectedGeometry;
        P.MeshRotation=FRotator(P.MeshRotation.Pitch,-P.MeshRotation.Yaw,-P.MeshRotation.Roll);Fixed.Add(P);
    }
    L.FixedPieces=MoveTemp(Fixed);
    L.OmegaSpawns.Reset();for(const auto& F:L.AlphaSpawns)L.OmegaSpawns.Add(ReflectFrame(F));
    auto Paths=[&](auto& Set){FoldPaths(Set);};
    Paths(L.Routes);Paths(L.SpawnExitRoutes);Paths(L.StairRoutes);Paths(L.RoomAccessRoutes);Paths(L.CrossingRoutes);Paths(L.DistrictRoutes);Paths(L.RooftopRoutes);Paths(L.GroundRoutes);Paths(L.RoofTransfers);Paths(L.VillagePassages);Paths(L.VillageSideBypasses);
    auto Points=[&](TArray<FVector>& Set)
    {TArray<FVector> Next;for(const auto& P:Set)if(P.Y<=0){Next.Add(P);if(P.Y<0)Next.Add(Reflect(P));}Set=MoveTemp(Next);};
    Points(L.ClimbPoints);Points(L.WallBridges);Points(L.RoomLightPoints);
    auto Boxes=[&](TArray<FBox>& Set)
    {TArray<FBox> Next;for(auto B:Set)if(B.Min.Y<0){B.Max.Y=FMath::Min(B.Max.Y,0.);Next.Add(B);Next.Add(ReflectBox(B));}Set=MoveTemp(Next);};
    Boxes(L.InaccessibleGround);Boxes(L.TownBuildings);Boxes(L.TownCourts);Boxes(L.TerrainPads);Boxes(L.LowCovers);Boxes(L.UnderpassBreaches);Boxes(L.ElevatedDecks);Boxes(L.OverheadClosures);Boxes(L.Plazas);Boxes(L.VillageSideWalls);Boxes(L.VillageFloorHoles);Boxes(L.WarehouseBaseCourts);Boxes(L.WarehouseCatwalks);
    auto Polygons=[&](TArray<TArray<FVector>>& Set)
    {
        TArray<TArray<FVector>> Next;
        for(const auto& Polygon:Set)
        {
            auto P=ClipPolygon(Polygon);if(P.Num()<3)continue;Next.Add(P);
            for(auto& V:P)V=Reflect(V);Algo::Reverse(P);Next.Add(MoveTemp(P));
        }
        Set=MoveTemp(Next);
    };
    Polygons(L.InaccessibleGroundPolygons);Polygons(L.TownFootprints);
    auto Frames=[&](auto& Set)
    {auto Next=Set;Next.Reset();for(auto P:Set)if(P.Frame.GetLocation().Y<=0){Next.Add(P);if(P.Frame.GetLocation().Y<0){P.Frame=ReflectFrame(P.Frame);Next.Add(P);}}Set=MoveTemp(Next);};
    Frames(L.Ladders);Frames(L.VillageUpperLadders);Frames(L.Containers);Frames(L.SecondaryStructures);Frames(L.Bases);Frames(L.VillageBlocks);
    TArray<FERLayoutCover> Covers;
    for(auto P:L.CoverSites)if(P.Frame.GetLocation().Y<0)
    {Covers.Add(P);P.Frame=ReflectFrame(P.Frame);for(auto* V:{&P.Hide,&P.Entry,&P.SightFrom,&P.SightTo})*V=Reflect(*V);Covers.Add(P);}
    L.CoverSites=MoveTemp(Covers);
    // Remaining specialized metadata must be reflected with its owning geometry.
    TArray<FERLayoutHouse> Houses;
    for(auto P:L.Houses)if(P.Frame.GetLocation().Y<0)
    {Houses.Add(P);P.Frame=ReflectFrame(P.Frame);for(auto& Path:P.Entrances)for(auto& V:Path)V=Reflect(V);for(auto& V:P.RoofMouths)V=Reflect(V);Houses.Add(P);}
    L.Houses=MoveTemp(Houses);
    TArray<FERLayoutTownGate> Gates;
    for(auto P:L.TownGates)if(P.Frame.GetLocation().Y<0){Gates.Add(P);P.Frame=ReflectFrame(P.Frame);for(auto& V:P.Passage)V=Reflect(V);Gates.Add(P);}L.TownGates=MoveTemp(Gates);
    TArray<FERAlleyRamp> Ramps;
    for(auto P:L.UnderpassRamps)if(P.Bounds.Min.Y<0){Ramps.Add(P);P.Bounds=ReflectBox(P.Bounds);P.Entry=Reflect(P.Entry);P.Exit=Reflect(P.Exit);Ramps.Add(P);}L.UnderpassRamps=MoveTemp(Ramps);
    auto SinglePath=[&](TArray<FVector>& Path)
    {TArray<TArray<FVector>> Parts{Path};FoldPaths(Parts);Path.Reset();for(const auto& Part:Parts)Path.Append(Part);};
    SinglePath(L.UpperRoute);SinglePath(L.LowerRoute);SinglePath(L.UnderpassRoute);SinglePath(L.UnderpassApproach);
    auto Transforms=[&](TArray<FTransform>& Set)
    {TArray<FTransform> Next;for(const auto& F:Set)if(F.GetLocation().Y<0){Next.Add(F);Next.Add(ReflectFrame(F));}Set=MoveTemp(Next);};
    Transforms(L.Steps);Transforms(L.StairSupports);Transforms(L.DistrictTowers);
    TArray<FERRoomsScreen> Screens;
    for(auto P:L.RoomScreens)if(P.Frame.GetLocation().Y<0){Screens.Add(P);P.Frame=ReflectFrame(P.Frame);Screens.Add(P);}L.RoomScreens=MoveTemp(Screens);
    // Indices into the folded route list are reconstructed from route elevation.
    L.WallAccessFlights.Reset();L.VillageAccessFlights.Reset();
    for(int32 I=0;I<L.StairRoutes.Num();++I)
    {if(!L.VillageBlocks.IsEmpty())L.VillageAccessFlights.Add(I);}
    TArray<FERRoomsFace> Shell;
    for(const auto& Face:L.RoomShell)
    {
        TArray<FVector> Polygon{Face.A,Face.B,Face.C};if(!Face.D.Equals(Face.C,.001) && !Face.D.Equals(Face.A,.001))Polygon.Add(Face.D);
        if(!Polygon.ContainsByPredicate([](FVector P){return P.Y<-.001;}))continue;
        Polygon=ClipPolygon(Polygon);
        for(int32 I=1;I+1<Polygon.Num();++I)
        {
            auto F=Face;F.A=Polygon[0];F.B=Polygon[I];F.C=F.D=Polygon[I+1];Shell.Add(F);
            F.A=Reflect(F.A);F.B=Reflect(Polygon[I+1]);F.C=F.D=Reflect(Polygon[I]);Shell.Add(F);
        }
    }
    L.RoomShell=MoveTemp(Shell);
    TArray<int32> Negative,Positive;Negative.Init(INDEX_NONE,L.RoomNodes.Num());Positive=Negative;
    TArray<FERRoomsNode> Nodes;TArray<FERRoomsLink> Links;
    for(int32 I=0;I<L.RoomNodes.Num();++I)
    {
        const auto& Original=L.RoomNodes[I];TArray<FVector> World;
        for(const auto P:Original.Polygon)World.Add(Original.Center+FVector(P,0));
        if(!World.ContainsByPredicate([](FVector P){return P.Y<-.001;}))continue;
        World=ClipPolygon(World);if(World.Num()<3)continue;
        auto Node=Original;Node.Polygon.Reset();FBox Bounds(ForceInit);
        for(const auto P:World)Bounds+=P;
        Node.Center=Bounds.GetCenter();Node.Size=FVector2D(Bounds.GetSize());
        for(const auto P:World)Node.Polygon.Add(FVector2D(P-Node.Center));
        Negative[I]=Nodes.Add(Node);Node.Center=Reflect(Node.Center);
        for(auto& P:Node.Polygon)P.Y=-P.Y;Algo::Reverse(Node.Polygon);Positive[I]=Nodes.Add(Node);
        if(Bounds.Max.Y>=-.001)
        {
            // A room cut by the axis remains one continuous chamber.
            const FVector Seam(Bounds.GetCenter().X,0,Original.Center.Z);
            Links.Add({Negative[I],Positive[I],Seam-FVector(0,1,0),Seam+FVector(0,1,0),float(Node.Size.X),Original.Height,0});
        }
    }
    for(auto Link:L.RoomLinks)
    {
        if(Link.Start.Y>=0 && Link.End.Y>=0)continue;
        const int32 NA=Negative.IsValidIndex(Link.A) ? Negative[Link.A]:INDEX_NONE,NB=Negative.IsValidIndex(Link.B) ? Negative[Link.B]:INDEX_NONE;
        if(NA==INDEX_NONE || NB==INDEX_NONE)
        {
            if((Link.Start.Y<0)!=(Link.End.Y<0))
            {
                const int32 From=Link.Start.Y<0 ? NA:NB;
                if(From!=INDEX_NONE)
                {
                    const FVector Start=Link.Start.Y<0 ? Link.Start:Link.End;
                    const FVector Seam=FMath::Lerp(Link.Start,Link.End,-Link.Start.Y/(Link.End.Y-Link.Start.Y));
                    Link.A=From;Link.B=From+1;Link.Start=Start;Link.End=Reflect(Start);
                    // The original negative half is a straight corridor to the seam.
                    // Its reflection is represented by two graph edges and a seam node.
                    FERRoomsNode Node;Node.Center=Seam;Node.Size={Link.Width,Link.Width};Node.Junction=true;Node.Height=Link.Height;
                    const float HalfWidth=Link.Width*.5f;Node.Polygon={{-HalfWidth,-HalfWidth},{HalfWidth,-HalfWidth},{HalfWidth,HalfWidth},{-HalfWidth,HalfWidth}};
                    const int32 At=Nodes.Add(Node);Link.B=At;Link.End=Seam;Links.Add(Link);
                    Link.A=At;Link.B=From+1;Link.Start=Seam;Link.End=Reflect(Start);Links.Add(Link);
                }
            }
            continue;
        }
        Link.A=NA;Link.B=NB;Links.Add(Link);Link.A=NA+1;Link.B=NB+1;Link.Start=Reflect(Link.Start);Link.End=Reflect(Link.End);Links.Add(Link);
    }
    L.RoomNodes=MoveTemp(Nodes);L.RoomLinks=MoveTemp(Links);
    L.Rooms.Reset();for(int32 I=0;I<L.RoomNodes.Num();++I){const auto& N=L.RoomNodes[I];L.Rooms.Add({FVector2D(N.Center)-N.Size*.5,FVector2D(N.Center)+N.Size*.5,I,float(N.Center.Z),0});}
    TArray<TArray<FVector2D>> Closed;
    for(const auto& Polygon:L.VillageClosedGround)
    {
        TArray<FVector> World;for(const auto P:Polygon)World.Add(FVector(P,0));World=ClipPolygon(World);if(World.Num()<3)continue;
        TArray<FVector2D> Plan;for(const auto P:World)Plan.Add(FVector2D(P));Closed.Add(Plan);
        for(auto& P:Plan)P.Y=-P.Y;Algo::Reverse(Plan);Closed.Add(Plan);
    }
    L.VillageClosedGround=MoveTemp(Closed);
    L.LayoutSymmetry=EERTemplateSymmetry::Mirror;
    return !L.Panels.IsEmpty() && !L.OmegaSpawns.IsEmpty();
}
}
