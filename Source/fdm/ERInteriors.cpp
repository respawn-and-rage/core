#include "ERProceduralMap.h"
#include "ERLayoutSolids.h"

// Shared furniture kit. A builder declares its rooms, courts and street edges; this pass turns each
// into a recognisable place whose pieces are also cover. It runs on an accepted layout and only adds
// solids: walls, routes, doors and starts stay where the builder put them.
namespace
{
constexpr float Aisle=110;   // Free-standing pieces keep a standing aisle on every side.
constexpr float Lane=90;     // Half-width kept clear along every recorded route.
constexpr float DoorReach=130,StartReach=120,WallGap=3;
// Furniture stops well inside the cell world's panel and storage limits.
constexpr int32 PanelBudget=3400;
constexpr int64 CellBudget=6500000;
int64 CellCount(const FERCellPanelDesc& P)
{
    int64 N=1;
    for(int32 Axis=0;Axis<3;++Axis)N*=FMath::Max(1,FMath::RoundToInt32(P.Cell[Axis]>0 ? P.Size[Axis]/P.Cell[Axis]:1.));
    return N;
}

enum class EItem : uint8
{
    Shelf,Cabinet,Wardrobe,Locker,Fridge,Bed,Sofa,Desk,Bunk,Dumpster,Planter,
    Table,Gondola,Unit,Counter,Crates,Pillar,LowWall,Partition,Stall
};
// Mode 0: destructible cells from the box. 1: one horizontal board. 2: fixed instanced metal.
struct FPart { FBox Box; EERBlockMaterial Material; uint8 Mode; };
struct FProgram { const TCHAR* Name; TArray<EItem> Walls,Islands; float Fill; bool Rows; };

const TArray<FProgram>& Programs(EERInteriorKind Kind)
{
    using E=EItem;
    static const TArray<FProgram> Shop{
        {TEXT("Grocery"),{E::Shelf,E::Shelf,E::Fridge},{E::Counter,E::Gondola,E::Gondola},.6f,true},
        {TEXT("Bar"),{E::Cabinet,E::Shelf,E::Fridge,E::Sofa},{E::Counter,E::Table,E::Table},.5f,false},
        {TEXT("Boutique"),{E::Shelf,E::Wardrobe,E::Cabinet},{E::Counter,E::Unit,E::Table},.5f,false},
        {TEXT("Agency"),{E::Cabinet,E::Locker,E::Sofa,E::Desk},{E::Counter,E::Partition,E::Table},.45f,true},
        {TEXT("Garage"),{E::Locker,E::Shelf,E::Cabinet},{E::Crates,E::Unit,E::Crates},.5f,false}};
    static const TArray<FProgram> BackRoom{
        {TEXT("Storeroom"),{E::Shelf,E::Locker,E::Shelf},{E::Crates,E::Crates},.65f,false},
        {TEXT("Kitchen"),{E::Cabinet,E::Fridge,E::Cabinet},{E::Unit},.6f,true},
        {TEXT("Office"),{E::Desk,E::Cabinet,E::Shelf},{E::Table},.5f,true}};
    static const TArray<FProgram> Home{
        {TEXT("Living"),{E::Sofa,E::Shelf,E::Cabinet},{E::Table},.5f,true},
        {TEXT("Kitchen"),{E::Cabinet,E::Fridge,E::Cabinet},{E::Unit},.6f,true},
        {TEXT("Bedroom"),{E::Bed,E::Wardrobe,E::Desk},{},.55f,true},
        {TEXT("Dining"),{E::Cabinet,E::Shelf},{E::Table},.4f,true},
        {TEXT("Study"),{E::Desk,E::Shelf,E::Sofa},{E::Unit},.5f,true}};
    static const TArray<FProgram> Hall{
        {TEXT("Depot"),{E::Shelf,E::Locker,E::Shelf},{E::Crates,E::Crates,E::Gondola,E::Partition},.45f,false},
        {TEXT("Mess"),{E::Cabinet,E::Fridge,E::Cabinet},{E::Table,E::Partition,E::Counter,E::LowWall},.4f,true},
        {TEXT("Barracks"),{E::Bunk,E::Locker,E::Bunk},{E::Partition,E::Unit,E::Crates},.5f,true},
        {TEXT("Columns"),{E::Cabinet,E::Locker},{E::Pillar,E::Pillar,E::LowWall},.25f,true},
        {TEXT("Plant"),{E::Locker,E::Cabinet,E::Desk},{E::Unit,E::Crates,E::Partition,E::Pillar},.45f,false}};
    static const TArray<FProgram> Court{
        {TEXT("Market"),{E::Dumpster,E::Planter},{E::Stall,E::Crates},.35f,true},
        {TEXT("Yard"),{E::Planter,E::Dumpster,E::Shelf},{E::Crates,E::LowWall},.35f,false}};
    static const TArray<FProgram> Street{
        {TEXT("Kerb"),{E::Planter,E::Dumpster,E::Planter},{},.22f,true}};
    switch(Kind)
    {
    case EERInteriorKind::BackRoom:return BackRoom;
    case EERInteriorKind::Home:return Home;
    case EERInteriorKind::Hall:return Hall;
    case EERInteriorKind::Court:return Court;
    case EERInteriorKind::Street:return Street;
    default:return Shop;
    }
}

// X runs along the piece. Placement recentres it, or moves its back onto a wall.
void Emit(EItem Item,FRandomStream& R,float Height,float Board,TArray<FPart>& Out)
{
    using M=EERBlockMaterial;
    auto Cells=[&](FVector Min,FVector Max,M Material=M::Wood){Out.Add({FBox(Min,Max),Material,0});};
    auto Fixed=[&](FVector Min,FVector Max){Out.Add({FBox(Min,Max),M::Metal,2});};
    auto Span=[&](int32 Lo,int32 Hi){return R.RandRange(Lo/20,Hi/20)*10.f;}; // Half of a 20cm-stepped length.
    auto Top=[&](float HalfX,float Y0,float Y1,float Z)
    {
        Out.Add({FBox(FVector(-HalfX,Y0,Z-Board),FVector(HalfX,Y1,Z)),M::Wood,1});
        for(float Side:{-1.f,1.f})
        {
            const float X=Side*(HalfX-10);
            Cells({X-Board*.5f,Y0+5,0},{X+Board*.5f,Y1-5,Z-Board});
        }
    };
    switch(Item)
    {
    case EItem::Shelf:{const float H=Span(180,300);Cells({-H,0,0},{H,45,200});break;}
    case EItem::Cabinet:{const float H=Span(160,240);Cells({-H,0,0},{H,60,95});break;}
    case EItem::Wardrobe:Cells({-80,0,0},{80,60,205});break;
    case EItem::Locker:{const float H=R.RandRange(2,4)*22.5f;Fixed({-H,0,0},{H,45,190});break;}
    case EItem::Fridge:Fixed({-40,0,0},{40,70,185});break;
    case EItem::Bed:Cells({-80,0,0},{80,Board,100});Cells({-80,4,0},{80,204,50});break;
    case EItem::Sofa:{const float H=Span(180,240);Cells({-H,4,0},{H,4+Board,90});Cells({-H,10,0},{H,90,45});break;}
    case EItem::Desk:Top(70,6,76,76);break;
    case EItem::Bunk:Cells({-100,0,0},{100,95,170});break;
    case EItem::Dumpster:Fixed({-100,0,0},{100,110,130});break;
    case EItem::Planter:{const float H=Span(180,260);Cells({-H,0,0},{H,60,90},M::Concrete);break;}
    case EItem::Table:Top(90,-45,45,76);break;
    case EItem::Gondola:{const float H=Span(200,300);Cells({-H,-30,0},{H,30,150});break;}
    case EItem::Unit:Cells({-90,-40,0},{90,40,95});break;
    case EItem::Counter:{const float H=Span(180,260);Cells({-H,-35,0},{H,35,105});break;}
    case EItem::Crates:
    {
        const float H=R.RandRange(5,7)*10.f,Tall=R.RandRange(0,2)==0 ? 170.f:R.RandRange(0,1)!=0 ? 120.f:90.f;
        Cells({-H,-H,0},{H,H,Tall});break;
    }
    case EItem::Pillar:Cells({-30,-30,0},{30,30,Height<=620 ? Height:300.f},M::Concrete);break;
    case EItem::LowWall:{const float H=Span(200,280);Cells({-H,-20,0},{H,20,105},M::Brick);break;}
    case EItem::Partition:{const float H=Span(200,300);Cells({-H,-10,0},{H,10,220},M::Concrete);break;}
    case EItem::Stall:
        Cells({-120,-40,0},{120,40,95});
        for(float X:{-134.f,134.f})for(float Y:{-94.f,94.f})Fixed({X-3,Y-3,0},{X+3,Y+3,230});
        Fixed({-140,-100,230},{140,100,236});break;
    }
}

// Quarter turns keep every box axis-aligned in the room frame.
FBox Turned(const FBox& B,int32 Quarter,FVector2D Offset)
{
    auto Turn=[&](FVector P)
    {
        const FVector2D Q=Quarter==0 ? FVector2D(P.X,P.Y):Quarter==1 ? FVector2D(-P.Y,P.X):Quarter==2 ? FVector2D(-P.X,-P.Y):FVector2D(P.Y,-P.X);
        return FVector(Q+Offset,P.Z);
    };
    const FVector A=Turn(B.Min),C=Turn(B.Max);
    return FBox(A.ComponentMin(C),A.ComponentMax(C));
}
}

namespace ERProcedural
{
void AddInteriorPair(FERProceduralLayout& L,FERLayoutInterior Interior,bool Mirror)
{
    Interior.Paired=true;Interior.Reflected=false;
    L.Interiors.Add(Interior);
    const FQuat Q=Interior.Frame.GetRotation();const FVector P=Interior.Frame.GetLocation();
    if(Mirror)
    {
        // Same orthonormal reflection as the mirror fold: local Y negates in the mate.
        auto Flip=[](FVector V){V.Y=-V.Y;return V;};
        const FMatrix Axes(Flip(Q.GetAxisX()),-Flip(Q.GetAxisY()),Flip(Q.GetAxisZ()),FVector::ZeroVector);
        Interior.Frame=FTransform(FQuat(Axes),Flip(P));Interior.Reflected=true;
        for(auto& Door:Interior.Doors)Door.Y=-Door.Y;
    }
    else
    {
        const FTransform Turn(FRotator(0,180,0));
        Interior.Frame=Interior.Frame*Turn;
        for(auto& Door:Interior.Doors)Door=Turn.TransformPosition(Door);
    }
    L.Interiors.Add(MoveTemp(Interior));
}

void FurnishInteriors(int32 Seed,FERProceduralLayout& L,const FERCellSettings& Cells)
{
    L.InteriorPanelStart=L.Panels.Num();L.InteriorFixedStart=L.FixedPieces.Num();
    if(L.Interiors.IsEmpty())return;
    const ERLayoutQueries::FLayoutSolids Known(L);
    TArray<TPair<FVector,FVector>> Segments;
    auto Paths=[&](const TArray<TArray<FVector>>& Set)
    {for(const auto& Path:Set)for(int32 I=1;I<Path.Num();++I)Segments.Emplace(Path[I-1],Path[I]);};
    for(const auto* Set:{&L.Routes,&L.SpawnExitRoutes,&L.StairRoutes,&L.RoomAccessRoutes,&L.CrossingRoutes,&L.DistrictRoutes,
        &L.RooftopRoutes,&L.GroundRoutes,&L.RoofTransfers,&L.VillagePassages,&L.VillageSideBypasses})Paths(*Set);
    Paths({L.UpperRoute,L.LowerRoute,L.UnderpassRoute,L.UnderpassApproach});
    for(const auto& Zone:L.SpawnZones)
    {
        // A pocket keeps its exit and the way from each start to its centre.
        Paths({Zone.ExitRoute});
        const auto& Bank=Zone.Team ? L.OmegaSpawns:L.AlphaSpawns;
        for(int32 I=Zone.FirstStart;I<Zone.FirstStart+Zone.StartCount && Bank.IsValidIndex(I);++I)Segments.Emplace(Bank[I].GetLocation(),Zone.Center);
    }
    for(const auto& Site:L.CoverSites)Segments.Emplace(Site.Entry,Site.Hide);
    int32 NextId=0;for(const auto& P:L.Panels)NextId=FMath::Max(NextId,P.Id/8+1);
    int64 CellsUsed=0;for(const auto& P:L.Panels)CellsUsed+=CellCount(P);
    const int32 Count=L.Interiors.Num();bool Open=true;int32 Mates=0;
    for(int32 Index=0;Index<Count;++Index)
    {
        const FERLayoutInterior I=L.Interiors[Index];
        // Mates are consecutive. Budgets close on a pair boundary, so they are furnished together.
        const bool Second=I.Paired && (Mates%2)!=0;const int32 Key=I.Paired ? Mates/2:0x4000+Index;
        if(I.Paired)++Mates;
        if(!Second)Open=L.Panels.Num()<PanelBudget && CellsUsed<CellBudget;
        if(!Open)continue;
        if(I.Half.X<120 || I.Half.Y<120 || !I.Frame.GetScale3D().Equals(FVector::OneVector,1.e-4))continue;
        // Mates share a room type and a budget. The arrangement is the same in half of the pairs;
        // one pair in four also swaps the second room's type within its family.
        const uint32 Pair=HashCombineFast(uint32(Seed),uint32(Key)*0x9e3779b9u+0x494e5445u);
        const auto& Family=Programs(I.Kind);
        const bool Vary=Second && (Pair&2u)!=0,Other=Second && (Pair>>4)%4u==0;
        const auto& Program=Family[((Pair>>8)+(Other ? 1u:0u))%uint32(Family.Num())];
        FRandomStream R(int32(Vary ? HashCombineFast(Pair,0x56415259u):Pair));
        const float Mirror=I.Reflected ? -1.f:1.f;
        // Everything below works in canonical room coordinates: a reflected mate negates Y.
        auto Local=[&](FVector World){FVector P=I.Frame.InverseTransformPositionNoScale(World);P.Y*=Mirror;return P;};
        const FBox2D Room(-I.Half,I.Half);
        TArray<FBox2D> Placed,Obstacles;TArray<uint8> Free;TArray<TPair<FVector2D,FVector2D>> Keep;TArray<FVector> Points;
        const FBox Reach=FBox(FVector(-I.Half.X-200,-I.Half.Y-200,-60),FVector(I.Half.X+200,I.Half.Y+200,I.Height+60)).TransformBy(I.Frame);
        for(const auto& S:Known.Solids)
        {
            if(!S.bPawn || !S.Bounds.Intersect(Reach))continue;
            FBox Box(ForceInit);
            for(int32 Corner=0;Corner<8;++Corner)
                Box+=Local(S.Frame.TransformPositionNoScale(FVector(Corner&1 ? S.Half.X:-S.Half.X,Corner&2 ? S.Half.Y:-S.Half.Y,Corner&4 ? S.Half.Z:-S.Half.Z)));
            if(Box.Max.Z<10 || Box.Min.Z>200)continue;
            const FBox2D Plan(FVector2D(Box.Min),FVector2D(Box.Max));
            if(Plan.Intersect(Room))Obstacles.Add(Plan);
        }
        // Sealed shells are triangles rather than boxes: ramps, galleries and clipped corners count too.
        for(const auto& F:L.RoomShell)
        {
            FBox Box(ForceInit);for(const FVector& Corner:{F.A,F.B,F.C,F.D})Box+=Local(Corner);
            if(Box.Max.Z<10 || Box.Min.Z>200)continue;
            const FBox2D Plan(FVector2D(Box.Min),FVector2D(Box.Max));
            if(Plan.Intersect(Room))Obstacles.Add(Plan);
        }
        for(const auto& S:Segments)
        {
            const FVector A=Local(S.Key),B=Local(S.Value);
            if((A.Z<-50 || A.Z>260) && (B.Z<-50 || B.Z>260))continue;
            if(FBox2D(FVector2D::Min(FVector2D(A),FVector2D(B)),FVector2D::Max(FVector2D(A),FVector2D(B))).ExpandBy(Lane).Intersect(Room))Keep.Emplace(FVector2D(A),FVector2D(B));
        }
        for(const auto& Door:I.Doors){const FVector P=Local(Door);Points.Add({P.X,P.Y,DoorReach});}
        for(const auto* Starts:{&L.AlphaSpawns,&L.OmegaSpawns})for(const auto& Start:*Starts)
        {
            const FVector P=Local(Start.GetLocation());
            if(P.Z>-50 && P.Z<260)Points.Add({P.X,P.Y,StartReach});
        }
        auto Clear=[&](const FBox2D& Rect,bool Island)
        {
            const float Inset=Island ? Aisle:WallGap-1;
            if(Rect.Min.X<-I.Half.X+Inset || Rect.Min.Y<-I.Half.Y+Inset || Rect.Max.X>I.Half.X-Inset || Rect.Max.Y>I.Half.Y-Inset)return false;
            if(I.Outline.Num()>=3)for(const FVector2D& Corner:{Rect.Min,Rect.Max,FVector2D(Rect.Min.X,Rect.Max.Y),FVector2D(Rect.Max.X,Rect.Min.Y)})
            {
                // Inside a convex outline of either winding, with the same inset as the walls.
                int32 Left=0,Right=0;
                for(int32 J=0;J<I.Outline.Num();++J)
                {
                    const FVector2D A=I.Outline[J],Edge=I.Outline[(J+1)%I.Outline.Num()]-A;
                    const double Side=FVector2D::CrossProduct(Edge,Corner-A)/FMath::Max(1.,Edge.Size());
                    if(Side>-Inset)++Right;if(Side<Inset)++Left;
                }
                if(Left!=I.Outline.Num() && Right!=I.Outline.Num())return false;
            }
            for(int32 J=0;J<Placed.Num();++J)
                if(Rect.ExpandBy(Island || Free[J] ? Aisle:-.5f).Intersect(Placed[J]))return false;
            for(const auto& S:Obstacles)if(Rect.ExpandBy(Island ? Aisle:1.f).Intersect(S))return false;
            for(const auto& P:Points)
            {
                const FVector2D D(FMath::Max3(Rect.Min.X-P.X,0.,P.X-Rect.Max.X),FMath::Max3(Rect.Min.Y-P.Y,0.,P.Y-Rect.Max.Y));
                if(D.SizeSquared()<FMath::Square(P.Z))return false;
            }
            const FBox Wide(FVector(Rect.Min-FVector2D(Lane),-1),FVector(Rect.Max+FVector2D(Lane),1));
            for(const auto& S:Keep)
            {
                const FVector A(S.Key,0),B(S.Value,0);
                if(Wide.IsInsideOrOn(A) || Wide.IsInsideOrOn(B) || FMath::LineBoxIntersection(Wide,A,B,B-A))return false;
            }
            return true;
        };
        int32 Pieces=0;
        auto Place=[&](const TArray<FPart>& Parts,int32 Quarter,FVector2D Offset,bool Island)
        {
            FBox Bounds(ForceInit);TArray<FBox> Boxes;
            for(const auto& Part:Parts){Boxes.Add(Turned(Part.Box,Quarter,Offset));Bounds+=Boxes.Last();}
            const FBox2D Rect(FVector2D(Bounds.Min),FVector2D(Bounds.Max));
            if(Bounds.Max.Z>I.Height+.5f)return false;
            if(!Clear(Rect,Island))return false;
            Placed.Add(Rect);Free.Add(Island ? 1:0);++Pieces;++L.InteriorPieces;
            for(int32 J=0;J<Parts.Num();++J)
            {
                FBox B=Boxes[J];
                if(I.Reflected){const double MinY=-B.Max.Y;B.Max.Y=-B.Min.Y;B.Min.Y=MinY;}
                if(Parts[J].Mode==2)
                {
                    FERLayoutFixedPiece Piece;Piece.Material=Parts[J].Material;
                    Piece.Transform=FTransform(I.Frame.GetRotation(),I.Frame.TransformPositionNoScale(B.GetCenter()),B.GetSize()/100);
                    L.FixedPieces.Add(Piece);continue;
                }
                const int32 First=L.Panels.Num();
                if(Parts[J].Mode==1)
                {
                    const FVector S=B.GetSize();auto& Panel=L.Panels.AddDefaulted_GetRef();
                    Panel.Id=NextId*8;Panel.Material=Parts[J].Material;
                    Panel.Transform=FTransform(FMatrix(FVector(1,0,0),FVector(0,0,1),FVector(0,-1,0),FVector(B.Min.X,B.Max.Y,B.Min.Z)));
                    Panel.Transform.SetScale3D(FVector::OneVector);
                    Panel.Size=FVector(S.X,S.Z,S.Y);
                    Panel.Cell=FVector(FERCellWorld::FitCell(S.X,Cells.CellSize),S.Z,FERCellWorld::FitCell(S.Y,Cells.CellSize));
                }
                else AddPieceCells(NextId,Parts[J].Material,B,false,Cells,L.Panels);
                ++NextId;
                for(int32 P=First;P<L.Panels.Num();++P){L.Panels[P].Transform=L.Panels[P].Transform*I.Frame;CellsUsed+=CellCount(L.Panels[P]);}
            }
            return true;
        };
        const double Area=4*I.Half.X*I.Half.Y/10000;
        const int32 Budget=FMath::Min(40,6+FMath::FloorToInt32(Area/8));
        // Backed pieces line the usable walls, leaving gaps and every door mouth open.
        for(int32 Wall=0;Wall<4 && !Program.Walls.IsEmpty();++Wall)if((I.Walls&(1<<Wall))!=0)
        {
            const bool AlongX=(Wall%2)==0;const float Run=AlongX ? I.Half.X:I.Half.Y,Across=AlongX ? I.Half.Y:I.Half.X;
            float U=-Run+60+R.RandRange(0,5)*20.f,Used=0;
            while(U<Run-100 && Used<Program.Fill*2*Run && Pieces<Budget)
            {
                TArray<FPart> Parts;Emit(Program.Walls[R.RandRange(0,Program.Walls.Num()-1)],R,I.Height,Cells.WoodDepth,Parts);
                FBox Bounds(ForceInit);for(const auto& Part:Parts)Bounds+=Part.Box;
                for(auto& Part:Parts)Part.Box=Part.Box.ShiftBy(FVector(-Bounds.GetCenter().X,-Bounds.Min.Y,0));
                const float Length=Bounds.GetSize().X,Center=U+Length*.5f,Back=Across-WallGap;
                const FVector2D Offset=Wall==0 ? FVector2D(Center,-Back):Wall==1 ? FVector2D(Back,Center):Wall==2 ? FVector2D(-Center,Back):FVector2D(-Back,-Center);
                if(Center+Length*.5f<=Run-60 && Place(Parts,Wall,Offset,false)){U+=Length+R.RandRange(0,4)*40.f;Used+=Length;}
                else U+=60;
            }
        }
        // Free-standing pieces break the long lines across the floor, on a row lattice or scattered.
        const int32 Wanted=Program.Islands.IsEmpty() ? 0:I.Kind==EERInteriorKind::Hall ? FMath::Clamp(FMath::FloorToInt32(Area/30),2,22):
            I.Kind==EERInteriorKind::Shop ? FMath::Min(8,FMath::FloorToInt32(Area/12)):I.Kind==EERInteriorKind::Court ? FMath::Clamp(FMath::FloorToInt32(Area/40),1,8):
            FMath::Min(4,FMath::FloorToInt32(Area/14));
        const int32 RowQuarter=(I.Half.X>=I.Half.Y)==((Pair&32u)!=0) ? 0:1;
        int32 Islands=0;
        for(int32 Try=0;Try<Wanted*14 && Islands<Wanted && Pieces<Budget;++Try)
        {
            TArray<FPart> Parts;Emit(Program.Islands[Islands%Program.Islands.Num()],R,I.Height,Cells.WoodDepth,Parts);
            FBox Bounds(ForceInit);for(const auto& Part:Parts)Bounds+=Part.Box;
            for(auto& Part:Parts)Part.Box=Part.Box.ShiftBy(FVector(-Bounds.GetCenter().X,-Bounds.GetCenter().Y,0));
            const int32 Quarter=Program.Rows ? RowQuarter:R.RandRange(0,1);
            const FVector2D Size(Bounds.GetSize()),Extent=(Quarter ? FVector2D(Size.Y,Size.X):Size)*.5;
            const FVector2D Limit=I.Half-Extent-FVector2D(Aisle);
            if(Limit.X<0 || Limit.Y<0)continue;
            FVector2D Center(R.RandRange(-FMath::FloorToInt32(Limit.X/20),FMath::FloorToInt32(Limit.X/20))*20.f,
                R.RandRange(-FMath::FloorToInt32(Limit.Y/20),FMath::FloorToInt32(Limit.Y/20))*20.f);
            if(Program.Rows)
            {
                // Parallel rows: quantise the coordinate across the row direction.
                const int32 Axis=Quarter ? 0:1;const double Pitch=2*Extent[Axis]+Aisle+60;
                Center[Axis]=FMath::Clamp(FMath::RoundToDouble(Center[Axis]/Pitch)*Pitch,-Limit[Axis],Limit[Axis]);
            }
            if(Place(Parts,Quarter,Center,true))++Islands;
        }
        if(Pieces>0)L.FeatureNames.AddUnique(FName(*FString::Printf(TEXT("Interior.%s"),Program.Name)));
    }
}
}
