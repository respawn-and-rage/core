#include "ERProceduralMap.h"
#include "ERLayoutSolids.h"
#include "ERMapPlayability.h"

// Template-independent pass over open ground. It reads only shared layout data (solids, routes,
// starts, exclusions, ground height), finds the widest uncovered floor on one half and builds a
// structure there with its mate: a lookout reached through a roof hatch, a walled compound with a
// firing step, a raised terrace with a crate climb, or a small cover group. Some entrances are
// boarded and open only when broken. It then cuts boarded breaches through builder walls and
// opens upper levels: roof slabs nothing reaches yet get a wall ladder, a doorway and windows.
// New templates inherit it without declaring anything.
namespace
{
constexpr float Cell=100,Margin=150,LaneHalf=170,StartReach=700,LookoutReach=1800;
using FPart=FERKitPart;
struct FFeature
{
    const TCHAR* Name=TEXT("");
    FVector2D Half=FVector2D::ZeroVector;
    TArray<FPart> Parts;
    TArray<FVector> Entry,Doors;
    bool bLadder=false,bRoom=false,bLookout=false;
    float Slope=20; // Ground height difference the footprint tolerates, in centimetres.
    FVector LadderFoot=FVector::ZeroVector;float Deck=0,Floor=0;
    TArray<FERLayoutBoulder> Rocks; // Centre heights are above the ground under each rock.
    EERInteriorKind Kind=EERInteriorKind::BackRoom;FVector2D RoomHalf=FVector2D::ZeroVector;
};
using M=EERBlockMaterial;

// A one-room house. The only way to its parapeted roof is the ladder inside, through a hatch.
FFeature Lookout(M Material,bool Boarded,float Sink,float Board)
{
    FFeature F;F.Name=Boarded ? TEXT("Ground.BoardedLookout"):TEXT("Ground.Lookout");F.Half={250,200};F.bLookout=true;F.Slope=90;
    const bool Timber=Material==M::Wood;const float Roof=300,Top=Roof+(Timber ? Board:20.f),DoorX=-120;
    auto Wall=[&](FVector Min,FVector Max,TArray<FBox> Holes={}){F.Parts.Add({FBox(Min,Max),Material,0,MoveTemp(Holes)});};
    Wall({-250,-200,-Sink},{250,-180,Roof},{FBox(FVector(DoorX-60,-200,0),FVector(DoorX+60,-180,220))});
    Wall({-250,180,-Sink},{250,200,Roof},{FBox(FVector(-90,180,125),FVector(90,200,165))});
    for(float Side:{-1.f,1.f})
        Wall({Side>0 ? 230.f:-250.f,-180,-Sink},{Side>0 ? 250.f:-230.f,180,Roof},{FBox(FVector(Side>0 ? 230.f:-250.f,-60,125),FVector(Side>0 ? 250.f:-230.f,60,165))});
    if(Sink>0)F.Parts.Add({FBox(FVector(-230,-180,-Sink),FVector(230,180,3)),M::Concrete,2,{}});
    // The roof leaves a hatch over the ladder and the standing climber.
    for(const FBox& Slab:{FBox(FVector(-250,-200,Roof),FVector(52,200,Top)),FBox(FVector(165,-200,Roof),FVector(250,200,Top)),
        FBox(FVector(52,-200,Roof),FVector(165,45,Top)),FBox(FVector(52,155,Roof),FVector(165,200,Top))})
        F.Parts.Add({Slab,Material,uint8(Timber ? 1:0),{}});
    Wall({-250,-200,Top},{250,-180,Top+100});Wall({-250,180,Top},{250,200,Top+100});
    Wall({-250,-180,Top},{-230,180,Top+100});Wall({230,-180,Top},{250,180,Top+100});
    // The ladder stands on the finished floor, which is the plinth top on sloping ground.
    const float Floor=Sink>0 ? 3.f:0.f;
    for(float Y:{61.f,139.f})F.Parts.Add({FBox(FVector(56,Y-4,Floor),FVector(64,Y+4,Top+110)),M::Metal,2,{}});
    for(float Z=Floor+25;Z<Top;Z+=25)F.Parts.Add({FBox(FVector(57,65,Z-3),FVector(63,135,Z+3)),M::Metal,2,{}});
    F.bLadder=true;F.LadderFoot={60,100,Floor};F.Deck=Top-Floor;F.Floor=Floor;
    if(Boarded)F.Parts.Add({FBox(FVector(DoorX-70,-204,0),FVector(DoorX+70,-204+Board,225)),M::Wood,0,{}});
    else F.Entry={{DoorX,-330,100},{DoorX,-90,100}};
    F.bRoom=true;F.Kind=EERInteriorKind::BackRoom;F.RoomHalf={226,176};
    F.Doors={{DoorX,-190,0},{115,100,0},{60,100,0}};
    return F;
}

// Head-high walls around a yard: an open gate, a boarded gap on the far side and a firing step.
FFeature Compound(M Material,float Sink,float Board)
{
    FFeature F;F.Name=TEXT("Ground.Compound");F.Half={350,300};
    auto Wall=[&](FVector Min,FVector Max,TArray<FBox> Holes={}){F.Parts.Add({FBox(Min,Max),Material,0,MoveTemp(Holes)});};
    Wall({-350,-300,-Sink},{350,-280,200},{FBox(FVector(-250,-300,-Sink),FVector(-110,-280,200))});
    Wall({-350,280,-Sink},{350,300,200},{FBox(FVector(110,280,-Sink),FVector(250,300,200))});
    Wall({-350,-280,-Sink},{-330,280,200});Wall({330,-280,-Sink},{350,280,200});
    // On sloping ground the yard is a level slab, so what stands in it does not float.
    if(Sink>0){F.Parts.Add({FBox(FVector(-330,-280,-Sink),FVector(330,280,3)),M::Concrete,2,{}});F.Floor=3;}
    F.Parts.Add({FBox(FVector(105,304,-Sink),FVector(255,304+Board,200)),M::Wood,0,{}});
    // Three 20cm rises to a 60cm platform: standing on it clears the wall.
    F.Parts.Add({FBox(FVector(-150,130,-Sink),FVector(-50,160,20)),M::Concrete,0,{}});
    F.Parts.Add({FBox(FVector(-150,160,-Sink),FVector(-50,190,40)),M::Concrete,0,{}});
    F.Parts.Add({FBox(FVector(-150,190,-Sink),FVector(-50,280,60)),M::Concrete,0,{}});
    F.Entry={{-180,-430,100},{-180,-150,100}};
    F.bRoom=true;F.Kind=EERInteriorKind::Court;F.RoomHalf={326,276};
    F.Doors={{-180,-290,0},{180,290,0},{-100,115,0}};
    return F;
}

// A platform above head height for whoever stands below it: seven low rises at the back, a parapet
// to crouch behind, and two timber crates on the flank that can be jumped, or shot away.
FFeature Terrace(M Material,float Sink)
{
    FFeature F;F.Name=TEXT("Ground.Terrace");F.Half={300,250};F.Slope=10;
    const float Top=140;
    F.Parts.Add({FBox(FVector(-150,-250,-Sink),FVector(200,250,Top)),M::Concrete,2,{}});
    F.Parts.Add({FBox(FVector(-300,-150,-Sink),FVector(-150,250,Top)),M::Concrete,2,{}});
    F.Parts.Add({FBox(FVector(200,50,-Sink),FVector(300,250,Top)),M::Concrete,2,{}});
    for(int32 Step=0;Step<6;++Step)
        F.Parts.Add({FBox(FVector(-300+Step*25.f,-250,-Sink),FVector(-275+Step*25.f,-150,20.f*(Step+1))),M::Concrete,2,{}});
    F.Parts.Add({FBox(FVector(200,-150,-Sink),FVector(300,-50,45)),M::Wood,0,{}});
    F.Parts.Add({FBox(FVector(200,-50,-Sink),FVector(300,50,95)),M::Wood,0,{}});
    for(float Side:{-1.f,1.f})
    {
        F.Parts.Add({FBox(FVector(Side>0 ? 60.f:-300.f,230,Top),FVector(Side>0 ? 300.f:-60.f,250,Top+90)),Material,3,{}});
        F.Parts.Add({FBox(FVector(Side>0 ? 280.f:-300.f,130,Top),FVector(Side>0 ? 300.f:-280.f,230,Top+90)),Material,3,{}});
    }
    F.Entry={{-450,-200,100},{-345,-200,100}};
    return F;
}

// Rocks rising out of natural ground: a low one, a waist-high one and a large one side by side.
// Each top is 50cm above the last, so the large rock can be climbed when it is not too tall.
FFeature Outcrop(uint32 Pick)
{
    FFeature F;F.Name=TEXT("Ground.Boulders");F.Half={240,180};F.Slope=150;
    const bool Tall=(Pick&8u)!=0;const float Grow=.9f+.2f*float((Pick>>8)%8u)/7;
    auto Rock=[&](FVector2D At,FVector2D Radius,float Top,uint32 Salt)
    {
        // The flat top stands Top above the ground and the widest girth a quarter of that.
        FERLayoutBoulder B;B.Radii=FVector(Radius.X*Grow,Radius.Y*Grow,Top/1.05f);
        B.Center=FVector(At,Top-B.Radii.Z*ERProcedural::BoulderTop);
        B.Shape=HashCombineFast(Pick,Salt);B.Yaw=float(B.Shape%360u);F.Rocks.Add(B);
    };
    Rock({-65,20},{165,135},Tall ? 215.f:150.f,1);
    Rock({85,-25},{100,90},100,2);
    Rock({165,-105},{68,60},50,3);
    return F;
}

// For floor too small for a structure: a corner to hold and a crate to crouch behind.
FFeature Cluster(M Material,bool Wide,float Sink)
{
    FFeature F;F.Name=TEXT("Ground.Cluster");F.Half={150,170};F.Slope=30;
    F.Parts.Add({FBox(FVector(-150,-20,-Sink),FVector(150,20,180)),Material,0,{}});
    F.Parts.Add({FBox(FVector(110,-160,-Sink),FVector(150,-20,180)),Material,0,{}});
    if(Wide)F.Parts.Add({FBox(FVector(-150,20,-Sink),FVector(-110,160,105)),Material,0,{}});
    F.Parts.Add({FBox(FVector(-130,-150,-Sink),FVector(-30,-50,110)),M::Wood,0,{}});
    return F;
}

double Ground(const FERProceduralLayout& L,FVector2D P){return ERProcedural::GroundHeight(L,P);}

bool Aligned(const FQuat& Q)
{
    for(const FVector& Axis:{Q.GetAxisX(),Q.GetAxisY(),Q.GetAxisZ()})if(Axis.GetAbsMax()<.9999)return false;
    return true;
}

// Cuts a world-axis box out of every plain fixed box it crosses. The remainders are appended, so
// they belong to the open-ground range. False when something in the way cannot be cut.
bool CarveFixed(FERProceduralLayout& L,const FBox& Hole,bool Apply)
{
    TArray<int32> Hit;
    for(int32 I=0;I<L.FixedPieces.Num();++I)
    {
        const auto& P=L.FixedPieces[I];
        const FTransform Frame(P.Transform.GetRotation(),P.Transform.GetLocation());
        const FVector Half=P.Transform.GetScale3D()*50;
        const FBox Bounds=FBox(-Half,Half).TransformBy(Frame);
        if(!Bounds.Intersect(Hole) || Bounds.Overlap(Hole).GetSize().GetMin()<.5)continue;
        if(P.Mesh || P.MeshCollision || !Aligned(Frame.GetRotation()))return false;
        Hit.Add(I);
    }
    if(!Apply)return true;
    for(int32 I:Hit)
    {
        const FERLayoutFixedPiece P=L.FixedPieces[I];
        const FTransform Frame(P.Transform.GetRotation(),P.Transform.GetLocation());
        const FVector Half=P.Transform.GetScale3D()*50;
        const FBox B(-Half,Half),C=B.Overlap(Hole.TransformBy(Frame.Inverse()));
        TArray<FBox> Rest;
        if(C.Min.X>B.Min.X+1)Rest.Add(FBox(B.Min,FVector(C.Min.X,B.Max.Y,B.Max.Z)));
        if(C.Max.X<B.Max.X-1)Rest.Add(FBox(FVector(C.Max.X,B.Min.Y,B.Min.Z),B.Max));
        if(C.Min.Y>B.Min.Y+1)Rest.Add(FBox(FVector(C.Min.X,B.Min.Y,B.Min.Z),FVector(C.Max.X,C.Min.Y,B.Max.Z)));
        if(C.Max.Y<B.Max.Y-1)Rest.Add(FBox(FVector(C.Min.X,C.Max.Y,B.Min.Z),FVector(C.Max.X,B.Max.Y,B.Max.Z)));
        if(C.Min.Z>B.Min.Z+1)Rest.Add(FBox(FVector(C.Min.X,C.Min.Y,B.Min.Z),FVector(C.Max.X,C.Max.Y,C.Min.Z)));
        if(C.Max.Z<B.Max.Z-1)Rest.Add(FBox(FVector(C.Min.X,C.Min.Y,C.Max.Z),FVector(C.Max.X,C.Max.Y,B.Max.Z)));
        for(const FBox& Part:Rest)
        {
            FERLayoutFixedPiece Piece=P;
            Piece.Transform=FTransform(Frame.GetRotation(),Frame.TransformPositionNoScale(Part.GetCenter()),Part.GetSize()/100);
            L.FixedPieces.Add(Piece);
        }
    }
    for(int32 H=Hit.Num()-1;H>=0;--H)
    {
        L.FixedPieces.RemoveAt(Hit[H]);
        if(Hit[H]<L.GroundFixedStart)--L.GroundFixedStart;
    }
    return true;
}
}

namespace ERProcedural
{
void AddKitParts(FERProceduralLayout& L,const TArray<FERKitPart>& Parts,const FTransform& Frame,bool Flip,int32& NextId,const FERCellSettings& Cells)
{
    auto Turned=[&](FBox B){if(Flip){const double Min=-B.Max.Y;B.Max.Y=-B.Min.Y;B.Min.Y=Min;}return B;};
    for(const auto& Part:Parts)
    {
        const FBox B=Turned(Part.Box);
        if(Part.Mode==2)
        {
            FERLayoutFixedPiece Piece;Piece.Material=Part.Material;
            Piece.Transform=FTransform(Frame.GetRotation(),Frame.TransformPositionNoScale(B.GetCenter()),B.GetSize()/100);
            L.FixedPieces.Add(Piece);continue;
        }
        const int32 First=L.Panels.Num();const FVector S=B.GetSize();
        if(Part.Mode==1)
        {
            auto& Panel=L.Panels.AddDefaulted_GetRef();Panel.Id=NextId*8;Panel.Material=Part.Material;
            Panel.Transform=FTransform(FMatrix(FVector(1,0,0),FVector(0,0,1),FVector(0,-1,0),FVector(B.Min.X,B.Max.Y,B.Min.Z)));
            Panel.Transform.SetScale3D(FVector::OneVector);Panel.Size=FVector(S.X,S.Z,S.Y);
            Panel.Cell=FVector(FERCellWorld::FitCell(S.X,Cells.CellSize),S.Z,FERCellWorld::FitCell(S.Y,Cells.CellSize));
        }
        else AddPieceCells(NextId,Part.Material,B,Part.Mode==3,Cells,L.Panels);
        ++NextId;
        // A wall is one panel whose X runs along its longer side from the box minimum.
        const int32 Axis=S.X>=S.Y ? 0:1;
        if(L.Panels.Num()==First+1)for(const auto& Hole:Part.Holes)
        {
            const FBox H=Turned(Hole);
            L.Panels[First].Holes.Add(FBox2D(FVector2D(H.Min[Axis]-B.Min[Axis],H.Min.Z-B.Min.Z),FVector2D(H.Max[Axis]-B.Min[Axis],H.Max.Z-B.Min.Z)));
        }
        for(int32 P=First;P<L.Panels.Num();++P)L.Panels[P].Transform=L.Panels[P].Transform*Frame;
    }
}

void EnrichOpenGround(int32 Seed,FERProceduralLayout& L,const FERCellSettings& Cells)
{
    L.GroundPanelStart=L.Panels.Num();L.GroundFixedStart=L.FixedPieces.Num();L.GroundLadderStart=L.Ladders.Num();
    // A sealed shell has no open ground, and an unpaired layout cannot place fair mates.
    if(!L.RoomShell.IsEmpty() || L.AlphaSpawns.IsEmpty() || L.AlphaSpawns.Num()!=L.OmegaSpawns.Num())return;
    bool Mirror=L.LayoutSymmetry==EERTemplateSymmetry::Mirror;
    if(L.LayoutSymmetry==EERTemplateSymmetry::None)
    {
        const FVector A=L.AlphaSpawns[0].GetLocation(),O=L.OmegaSpawns[0].GetLocation();
        if(O.Equals(FVector(A.X,-A.Y,A.Z),5))Mirror=true;
        else if(!O.Equals(FVector(-A.X,-A.Y,A.Z),5))return;
    }
    const FVector2D Arena(L.ArenaSize);
    const int32 NX=FMath::CeilToInt32(Arena.X/Cell),NY=FMath::CeilToInt32(Arena.Y/Cell);
    if(NX<12 || NY<12 || NX*NY>160000)return;
    const FVector2D Origin=-Arena*.5;
    TArray<uint8> Blocked,Lane,Tried;Blocked.Init(0,NX*NY);Lane.Init(0,NX*NY);Tried.Init(0,NX*NY);
    auto Center=[&](int32 X,int32 Y){return Origin+FVector2D((X+.5)*Cell,(Y+.5)*Cell);};
    auto Mark=[&](TArray<uint8>& Grid,FVector2D Min,FVector2D Max)
    {
        const int32 X0=FMath::Clamp(FMath::FloorToInt32((Min.X-Origin.X)/Cell),0,NX-1),X1=FMath::Clamp(FMath::FloorToInt32((Max.X-Origin.X)/Cell),0,NX-1);
        const int32 Y0=FMath::Clamp(FMath::FloorToInt32((Min.Y-Origin.Y)/Cell),0,NY-1),Y1=FMath::Clamp(FMath::FloorToInt32((Max.Y-Origin.Y)/Cell),0,NY-1);
        if(Max.X<Origin.X || Max.Y<Origin.Y || Min.X>-Origin.X || Min.Y>-Origin.Y)return;
        for(int32 Y=Y0;Y<=Y1;++Y)for(int32 X=X0;X<=X1;++X)Grid[Y*NX+X]=1;
    };
    auto MarkBox=[&](const FBox& B){Mark(Blocked,FVector2D(B.Min),FVector2D(B.Max));};
    // Everything that is not walkable open floor: the boundary, exclusions, water, pads and solids.
    for(int32 Y=0;Y<NY;++Y)for(int32 X=0;X<NX;++X)
    {
        const FVector2D P=Center(X,Y);
        bool Out=FMath::Abs(P.X)>Arena.X*.5-Margin || FMath::Abs(P.Y)>Arena.Y*.5-Margin;
        if(L.CircularArena)Out|=FMath::Square(P.X/(Arena.X*.5-Margin))+FMath::Square(P.Y/(Arena.Y*.5-Margin))>1;
        if(!L.VillageStreams.IsEmpty() || !L.VillageRingOuter.IsEmpty())Out|=VillageStreamAt(L,P)!=nullptr || VillageRingContains(L,P);
        for(const auto& Polygon:L.InaccessibleGroundPolygons)
        {
            bool Inside=false;
            for(int32 I=0,J=Polygon.Num()-1;I<Polygon.Num();J=I++)
                if((Polygon[I].Y>P.Y)!=(Polygon[J].Y>P.Y) && P.X<(Polygon[J].X-Polygon[I].X)*(P.Y-Polygon[I].Y)/(Polygon[J].Y-Polygon[I].Y)+Polygon[I].X)Inside=!Inside;
            Out|=Inside;
        }
        if(Out)Blocked[Y*NX+X]=1;
    }
    // Levelled pads, courts and plazas are still open floor: only what stands on them is blocked.
    const TArray<uint8> Outside=Blocked;
    for(const auto* Set:{&L.InaccessibleGround,&L.PoolWater,&L.WarehouseBaseCourts,&L.UnderpassBreaches,&L.ElevatedDecks,&L.OverheadClosures,&L.VillageFloorHoles,&L.LowCovers})
        for(const auto& Box:*Set)MarkBox(Box);
    for(const auto& Ramp:L.UnderpassRamps)MarkBox(Ramp.Bounds);
    const ERLayoutQueries::FLayoutSolids Known(L);
    for(const auto& S:Known.Solids)
    {
        const double Floor=Ground(L,FVector2D(S.Bounds.GetCenter()));
        if(S.bPawn && S.Bounds.Max.Z>Floor+25 && S.Bounds.Min.Z<Floor+450)MarkBox(S.Bounds);
    }
    for(const auto& P:L.Panels)if(!P.Skin.IsEmpty())MarkBox(FBox(FVector::ZeroVector,P.Size).TransformBy(P.Transform));
    for(const auto& P:L.FixedPieces)if(P.MeshCollision)MarkBox(FBox(-P.Transform.GetScale3D()*50,P.Transform.GetScale3D()*50).TransformBy(FTransform(P.Transform.GetRotation(),P.Transform.GetLocation())));
    for(const auto& Space:L.Interiors)MarkBox(FBox(FVector(-Space.Half,0),FVector(Space.Half,1)).TransformBy(Space.Frame));
    for(const auto& House:L.Houses)
    {
        const FVector Half=House.Frame.GetScale3D()*.5;
        MarkBox(FBox(FVector(-Half.X,-Half.Y,0),FVector(Half.X,Half.Y,1)).TransformBy(FTransform(House.Frame.GetRotation(),House.Frame.GetLocation())));
    }
    // Lanes are open floor that must stay open: routes, starts, pockets, cover entries and ladders.
    auto Keep=[&](FVector P,float Reach){Mark(Lane,FVector2D(P)-FVector2D(Reach),FVector2D(P)+FVector2D(Reach));};
    auto Paths=[&](const TArray<TArray<FVector>>& Set)
    {
        for(const auto& Path:Set)for(int32 I=1;I<Path.Num();++I)
        {
            const FVector A=Path[I-1],B=Path[I];
            if(A.Z>Ground(L,FVector2D(A))+260 && B.Z>Ground(L,FVector2D(B))+260)continue;
            const int32 Steps=FMath::Max(1,FMath::CeilToInt32(FVector::Dist2D(A,B)/50));
            for(int32 S=0;S<=Steps;++S)Keep(FMath::Lerp(A,B,double(S)/Steps),LaneHalf);
        }
    };
    for(const auto* Set:{&L.Routes,&L.SpawnExitRoutes,&L.StairRoutes,&L.RoomAccessRoutes,&L.CrossingRoutes,&L.DistrictRoutes,
        &L.GroundRoutes,&L.VillagePassages,&L.VillageSideBypasses,&L.RockScrambles})Paths(*Set);
    if(L.RockyCoverOnly)
        for(const auto& Route:L.RooftopRoutes)for(const auto& Point:Route)Keep(Point,350);
    Paths({L.UpperRoute,L.LowerRoute,L.UnderpassRoute,L.UnderpassApproach});
    TArray<FVector> Starts;
    for(const auto* Set:{&L.AlphaSpawns,&L.OmegaSpawns})for(const auto& Start:*Set){Starts.Add(Start.GetLocation());Keep(Start.GetLocation(),StartReach);}
    for(const auto& Zone:L.SpawnZones){Paths({Zone.ExitRoute});Keep(Zone.Center,StartReach);}
    for(const auto& Site:L.CoverSites){Keep(Site.Hide,250);Keep(Site.Entry,250);}
    for(const auto& Ladder:L.Ladders)Keep(Ladder.Frame.GetLocation(),250);
    for(const auto& Entrance:L.CentralEntrances)Keep(Entrance.GetLocation(),300);
    // Chebyshev distance from every open cell to the nearest blocked one, in cells.
    TArray<int16> Open;
    auto Measure=[&]
    {
        Open.Init(INT16_MAX,NX*NY);TArray<int32> Queue;Queue.Reserve(NX*NY);
        for(int32 I=0;I<NX*NY;++I)if(Blocked[I]){Open[I]=0;Queue.Add(I);}
        for(int32 Q=0;Q<Queue.Num();++Q)
        {
            const int32 X=Queue[Q]%NX,Y=Queue[Q]/NX;
            for(int32 DY=-1;DY<=1;++DY)for(int32 DX=-1;DX<=1;++DX)
            {
                const int32 AX=X+DX,AY=Y+DY;
                if(AX<0 || AY<0 || AX>=NX || AY>=NY || Open[AY*NX+AX]<=Open[Queue[Q]]+1)continue;
                Open[AY*NX+AX]=Open[Queue[Q]]+1;Queue.Add(AY*NX+AX);
            }
        }
    };
    auto Free=[&](FVector2D Min,FVector2D Max)
    {
        if(Min.X<Origin.X || Min.Y<Origin.Y || Max.X>-Origin.X || Max.Y>-Origin.Y)return false;
        for(int32 Y=FMath::FloorToInt32((Min.Y-Origin.Y)/Cell);Y<=FMath::Min(NY-1,FMath::FloorToInt32((Max.Y-Origin.Y)/Cell));++Y)
            for(int32 X=FMath::FloorToInt32((Min.X-Origin.X)/Cell);X<=FMath::Min(NX-1,FMath::FloorToInt32((Max.X-Origin.X)/Cell));++X)
                if(Blocked[Y*NX+X] || Lane[Y*NX+X])return false;
        return true;
    };
    int32 Wood=0;for(const auto& P:L.Panels)Wood+=P.Material==M::Wood ? 1:-1;
    FRandomStream R(int32(HashCombineFast(uint32(Seed),0x47524e44u)));
    const M Style=Wood>0 ? M::Wood:R.RandRange(0,1)!=0 ? M::Brick:M::Concrete;
    const bool Graded=L.GradedTerrain || L.CircularArena || !L.VillageBlocks.IsEmpty();
    // Highest and lowest ground under a footprint: a structure stands level on its own plinth.
    auto Level=[&](FVector2D P,FVector2D Half,double& High,double& Low)
    {
        High=-TNumericLimits<double>::Max();Low=TNumericLimits<double>::Max();
        for(int32 Y=-2;Y<=2;++Y)for(int32 X=-2;X<=2;++X)
        {const double H=Ground(L,P+FVector2D(Half.X*X*.5,Half.Y*Y*.5));High=FMath::Max(High,H);Low=FMath::Min(Low,H);}
    };
    int32 NextId=0;for(const auto& P:L.Panels)NextId=FMath::Max(NextId,P.Id/8+1);
    auto Commit=[&](const FFeature& F,const FTransform& Frame,bool Flip)
    {
        auto Point=[&](FVector P){if(Flip)P.Y=-P.Y;return Frame.TransformPositionNoScale(P);};
        AddKitParts(L,F.Parts,Frame,Flip,NextId,Cells);
        for(int32 I=0;I<F.Rocks.Num();++I)
        {
            // A rock follows the ground it stands on, so a slope shows more of it on the low side.
            FERLayoutBoulder Rock=F.Rocks[I];const FVector At=Point(Rock.Center);
            Rock.Center=FVector(At.X,At.Y,Ground(L,FVector2D(At))+Rock.Center.Z);
            Rock.Yaw=float(Frame.GetRotation().Rotator().Yaw)+(Flip ? -Rock.Yaw:Rock.Yaw);Rock.Reflected=Flip;
            L.Boulders.Add(Rock);
        }
        if(F.bLadder)L.Ladders.Add({FTransform(Frame.GetRotation(),Point(F.LadderFoot)),F.Deck+5,F.Deck});
        if(!F.Entry.IsEmpty()){TArray<FVector> Path;for(const auto& P:F.Entry)Path.Add(Point(P));L.RoomAccessRoutes.Add(MoveTemp(Path));}
    };
    const int32 Budget=FMath::Clamp(FMath::RoundToInt32(Arena.X*Arena.Y/10000/700),2,10);
    // The largest pair is about 0.3M cells: stop while one more still fits the layout's storage.
    const int64 CellRoom=SharedCellRoom(L);
    auto CellsAdded=[&]{int64 N=0;for(int32 P=L.GroundPanelStart;P<L.Panels.Num();++P)N+=PanelCellCount(L.Panels[P]);return N;};
    int32 Pairs=0,Lookouts=0,Compounds=0,Terraces=0,Boulders=0,Spots=0,Crowded=0,Steep=0,Widest=0;bool Dirty=true;
    // Rocks belong to natural ground: often where a template has terrain, seldom between its houses.
    const uint32 RockShare=!Graded ? 0u:(!L.Houses.IsEmpty() || !L.TownBuildings.IsEmpty()) ? 2u:6u;
    for(int32 Guard=0;Guard<2500 && Pairs<Budget && L.Panels.Num()<3000;++Guard)
    {
        if(Dirty){if(CellsAdded()+400000>CellRoom)break;Measure();Dirty=false;}
        // The widest uncovered floor on the sampled half, in a stable order.
        int32 Best=INDEX_NONE;
        for(int32 Y=0;Y<NY;++Y)for(int32 X=0;X<NX;++X)
        {
            const int32 I=Y*NX+X;
            if(Blocked[I] || Lane[I] || Tried[I] || Center(X,Y).Y>-Margin || (Best!=INDEX_NONE && Open[I]<=Open[Best]))continue;
            Best=I;
        }
        if(Best==INDEX_NONE || Open[Best]<4)break;
        ++Spots;Widest=FMath::Max(Widest,int32(Open[Best]));
        // A spot that takes nothing rules out its neighbours too.
        for(int32 DY=-1;DY<=1;++DY)for(int32 DX=-1;DX<=1;++DX)
            if(Best%NX+DX>=0 && Best%NX+DX<NX && Best/NX+DY>=0 && Best/NX+DY<NY)Tried[Best+DY*NX+DX]=1;
        const FVector2D P=Center(Best%NX,Best/NX);
        double Nearest=TNumericLimits<double>::Max();for(const auto& Start:Starts)Nearest=FMath::Min(Nearest,FVector2D::Distance(P,FVector2D(Start)));
        // The large kinds take turns, the one built least going first; a cover group fits anywhere.
        const uint32 Pick=HashCombineFast(uint32(Seed),uint32(Pairs)*0x9e3779b9u+0x4f50454eu);
        TArray<int32> Kinds;
        if(L.RockyCoverOnly)Kinds.Add(4);
        else
        {
            if(Open[Best]>=5 && Lookouts<2 && Nearest>=LookoutReach)Kinds.Add(0);
            if(Open[Best]>=6 && Compounds<2)Kinds.Add(1);
            if(Open[Best]>=5 && Terraces<2 && Nearest>=1200)Kinds.Add(3);
            // Where the ground is natural and not between houses, rocks are a kind of their own.
            if(RockShare>=6 && Boulders<3)Kinds.Add(4);
            auto Count=[&](int32 Kind){return Kind==0 ? Lookouts:Kind==1 ? Compounds:Kind==3 ? Terraces:Boulders;};
            Kinds.StableSort([&](int32 A,int32 B){return Count(A)!=Count(B) ? Count(A)<Count(B):(A+Pick)%5u<(B+Pick)%5u;});
            if((Pick>>12)%10u<RockShare && !Kinds.Contains(4))Kinds.Add(4);
            Kinds.Add(2);
        }
        // The slit, the firing step and the corner face the middle of the arena.
        const FVector2D In=(-P).GetSafeNormal();
        int32 Facing=0;double Dot=-2;
        for(int32 Q=0;Q<4;++Q){const FVector2D Y(Q==0 ? 0:Q==1 ? -1:Q==2 ? 0:1,Q==0 ? 1:Q==1 ? 0:Q==2 ? -1:0);if(FVector2D::DotProduct(Y,In)>Dot){Dot=FVector2D::DotProduct(Y,In);Facing=Q;}}
        bool Built=false;
        for(int32 K=0;K<Kinds.Num() && !Built;++K)for(int32 Turn=0;Turn<4 && !Built;++Turn)
        {
            const int32 Q=(Facing+(Turn==0 ? 0:Turn==1 ? 1:Turn==2 ? 3:2))%4;
            const FVector2D Size=Kinds[K]==0 ? FVector2D(250,200):Kinds[K]==1 ? FVector2D(350,300):Kinds[K]==3 ? FVector2D(300,250):Kinds[K]==4 ? FVector2D(240,180):FVector2D(150,170);
            const FVector2D Half=(Q%2) ? FVector2D(Size.Y,Size.X):Size,Reach=Half+FVector2D(Margin);
            const FVector2D MateP=Mirror ? FVector2D(P.X,-P.Y):-P;
            if(!Free(P-Reach,P+Reach) || !Free(MateP-Reach,MateP+Reach) || P.Y+Reach.Y>MateP.Y-Reach.Y){++Crowded;continue;}
            double Base=0,Low=0,MateBase=0,MateLow=0;
            if(Graded){Level(P,Half,Base,Low);Level(MateP,Half,MateBase,MateLow);}
            const float Sink=Graded ? float(FMath::Max(Base-Low,MateBase-MateLow))+40:0.f;
            const FFeature F=Kinds[K]==0 ? Lookout(Style,(Pick&1u)!=0,Sink,Cells.WoodDepth):Kinds[K]==1 ? Compound(Style,Sink,Cells.WoodDepth):Kinds[K]==3 ? Terrace(Style,Sink):Kinds[K]==4 ? Outcrop(Pick):Cluster(Style,(Pick&2u)!=0,Sink);
            if(Sink-40>F.Slope){++Steep;continue;}
            const FTransform Frame(FRotator(0,Q*90.,0),FVector(P,Base));
            const FTransform MateFrame(FRotator(0,Mirror ? -Q*90.:Q*90.+180,0),FVector(MateP,MateBase));
            // The doorway opens on the uphill side, where the ground meets the floor.
            if(Graded && !F.Entry.IsEmpty() && Ground(L,FVector2D(Frame.TransformPositionNoScale(F.Entry[0])))<Base-25)continue;
            if(Graded && F.bLookout && F.Entry.IsEmpty() && Ground(L,FVector2D(Frame.TransformPositionNoScale(FVector(-120,-300,0))))<Base-25)continue;
            Commit(F,Frame,false);Commit(F,MateFrame,Mirror);
            if(F.bRoom)
            {
                FERLayoutInterior Space;Space.Frame=FTransform(Frame.GetRotation(),Frame.TransformPositionNoScale(FVector(0,0,F.Floor)));Space.Half=F.RoomHalf;Space.Kind=F.Kind;Space.Height=F.bLookout ? 300.f:400.f;
                for(const auto& Door:F.Doors)Space.Doors.Add(Frame.TransformPositionNoScale(Door));
                AddInteriorPair(L,MoveTemp(Space),Mirror);
            }
            Mark(Blocked,P-Half,P+Half);Mark(Blocked,MateP-Half,MateP+Half);
            L.FeatureNames.AddUnique(F.Name);++L.GroundFeatures;++Pairs;Built=true;Dirty=true;
            if(Kinds[K]==0)++Lookouts;else if(Kinds[K]==1)++Compounds;else if(Kinds[K]==3)++Terraces;else if(Kinds[K]==4)++Boulders;
        }
    }
    // Boarded breaches: a door-sized gap through a builder's masonry wall, closed by timber boards.
    // Both sides must be standing floor beside a recorded route, so breaking the boards joins two
    // ways that the intact map keeps apart. Dense templates get their hidden ways from this rule.
    const int32 Own=L.GroundPanelStart;int32 Breaches=0;TArray<FVector> Cuts;
    auto Sym=[&](FVector P){return Mirror ? FVector(P.X,-P.Y,P.Z):FVector(-P.X,-P.Y,P.Z);};
    auto Span=[&](int32 Index,double Along)
    {
        const auto& P=L.Panels[Index];
        if(!P.Skin.IsEmpty() || P.bPane || P.bIndestructible || !P.bAnchorBottom || P.Strength!=1 || (P.Material!=M::Brick && P.Material!=M::Concrete) ||
           P.Transform.GetRotation().GetAxisZ().Z<.999 || P.Size.X<400 || P.Size.Z<220 || P.Size.Y>45 || Along<150 || Along>P.Size.X-150)return false;
        for(const auto& Hole:P.Holes)if(Along>Hole.Min.X-300 && Along<Hole.Max.X+300)return false;
        return true;
    };
    auto Standing=[&](FVector P,FVector Along)
    {
        const int32 X=FMath::FloorToInt32((P.X-Origin.X)/Cell),Y=FMath::FloorToInt32((P.Y-Origin.Y)/Cell);
        if(X<3 || Y<3 || X>=NX-3 || Y>=NY-3 || Outside[Y*NX+X] || FMath::Abs(Ground(L,FVector2D(P))+100-P.Z)>30 || !Known.PawnClear(P-Along*40,P+Along*40))return false;
        for(const auto& Court:L.WarehouseBaseCourts)if(Court.ExpandBy(FVector(150,150,10000)).IsInsideOrOn(P))return false;
        for(int32 DY=-3;DY<=3;++DY)for(int32 DX=-3;DX<=3;++DX)if(Lane[(Y+DY)*NX+X+DX])return true;
        return false;
    };
    auto Gap=[&](int32 Index,double Along){L.Panels[Index].Holes.Add(FBox2D(FVector2D(Along-55,0),FVector2D(Along+55,210)));};
    auto Board=[&](int32 Index,double Along)
    {
        const FTransform Wall=L.Panels[Index].Transform;const float Depth=L.Panels[Index].Size.Y;
        const int32 First=L.Panels.Num();
        AddPieceCells(NextId++,M::Wood,FBox(FVector(Along-62,(Depth-Cells.WoodDepth)*.5,0),FVector(Along+62,(Depth+Cells.WoodDepth)*.5,215)),true,Cells,L.Panels);
        for(int32 P=First;P<L.Panels.Num();++P)L.Panels[P].Transform=L.Panels[P].Transform*Wall;
        const FVector Point=Wall.TransformPositionNoScale(FVector(Along,Depth*.5,0));
        for(auto& Space:L.Interiors)
            if(FBox(FVector(-Space.Half-FVector2D(40),-50),FVector(Space.Half+FVector2D(40),300)).IsInsideOrOn(Space.Frame.InverseTransformPositionNoScale(Point)))Space.Doors.Add(Point);
    };
    const int32 BreachBudget=FMath::Clamp(FMath::RoundToInt32(Arena.X*Arena.Y/10000/1500),1,4);
    for(int32 Step=0;Step<Own && Breaches<BreachBudget;++Step)
    {
        const int32 Index=int32((uint32(R.GetInitialSeed())+uint32(Step)*7919u)%uint32(Own));
        const double Along=FMath::GridSnap(L.Panels[Index].Size.X*(.35+.15*((Step+Index)%3)),5.);
        if(!Span(Index,Along))continue;
        const FTransform Wall=L.Panels[Index].Transform;
        const FVector Point=Wall.TransformPositionNoScale(FVector(Along,L.Panels[Index].Size.Y*.5,100));
        const FVector Normal=Wall.GetRotation().GetAxisY(),Run=Wall.GetRotation().GetAxisX(),Mate=Sym(Point);
        if(Point.Y>5 || !Standing(Point+Normal*150,Run) || !Standing(Point-Normal*150,Run))continue;
        bool Far=true;
        for(const auto& Start:Starts)Far&=FVector::Dist2D(Start,Point)>=1500;
        for(const auto& Cut:Cuts)Far&=FVector::Dist2D(Cut,Point)>=1200;
        if(!Far)continue;
        // The mate gap lies in the mate wall, or is this very gap on a wall along the seam.
        int32 Other=INDEX_NONE;double OtherAlong=0;
        if(!Mate.Equals(Point,5))
        {
            for(int32 J=0;J<Own && Other==INDEX_NONE;++J)
            {
                const FVector Local=L.Panels[J].Transform.InverseTransformPositionNoScale(Mate);
                if(Local.X>0 && Local.X<L.Panels[J].Size.X && Local.Y>-1 && Local.Y<L.Panels[J].Size.Y+1 && FMath::Abs(Local.Z-100)<30){Other=J;OtherAlong=FMath::GridSnap(Local.X,5.);}
            }
            if(Other==INDEX_NONE || !Span(Other,OtherAlong) || (Other==Index && FMath::Abs(OtherAlong-Along)<400))continue;
        }
        // Boards stop a rifle round far less than masonry does: a gap is cut only where the open
        // hole itself would still leave every opposing pair of starts screened.
        Gap(Index,Along);if(Other!=INDEX_NONE)Gap(Other,OtherAlong);
        if(!ERPlayability::InitialSpawnsOccluded(L))
        {
            L.Panels[Index].Holes.Pop();if(Other!=INDEX_NONE)L.Panels[Other].Holes.Pop();
            continue;
        }
        Board(Index,Along);if(Other!=INDEX_NONE)Board(Other,OtherAlong);
        Cuts.Add(Point);Cuts.Add(Mate);++Breaches;++L.GroundFeatures;
        L.FeatureNames.AddUnique(TEXT("Ground.BoardedBreach"));
    }
    // Upper levels: a roof slab of the builder that nothing reaches yet gets a ladder up its wall,
    // away from the routes where it can. Under open sky that makes a roof terrace. Under a ceiling
    // it makes an upper room: a doorway is cut at the head of the ladder and the glazed plates on
    // its walls are cut open as windows. Neither may overlook a start.
    struct FRoof{FBox Box;int32 Group;};
    TArray<FRoof> Roofs;
    for(int32 I=0;I<L.GroundFixedStart;++I)
    {
        const auto& P=L.FixedPieces[I];
        const FTransform Frame(P.Transform.GetRotation(),P.Transform.GetLocation());
        if(!P.Roof || P.Mesh || P.MeshCollision || P.VisualOnly || !Aligned(Frame.GetRotation()))continue;
        const FVector Half=P.Transform.GetScale3D()*50;
        const FBox Box=FBox(-Half,Half).TransformBy(Frame);
        const double Rise=Box.Max.Z-Ground(L,FVector2D(Box.GetCenter()));
        if(Rise<250 || Rise>450 || Box.GetSize().Z>60 || FMath::Min(Box.GetSize().X,Box.GetSize().Y)<200)continue;
        Roofs.Add({Box,Roofs.Num()});
    }
    // Slabs that touch at one height are one level.
    for(bool Merged=true;Merged;)
    {
        Merged=false;
        for(int32 I=0;I<Roofs.Num();++I)for(int32 J=I+1;J<Roofs.Num();++J)
            if(Roofs[I].Group!=Roofs[J].Group && FMath::Abs(Roofs[I].Box.Max.Z-Roofs[J].Box.Max.Z)<1 && Roofs[I].Box.ExpandBy(FVector(5,5,0)).Intersect(Roofs[J].Box))
            {Roofs[I].Group=Roofs[J].Group=FMath::Min(Roofs[I].Group,Roofs[J].Group);Merged=true;}
    }
    auto SymDir=[&](FVector D){return Mirror ? FVector(D.X,-D.Y,0):FVector(-D.X,-D.Y,0);};
    auto SymBox=[&](const FBox& B){return FBox(ForceInit)+Sym(B.Min)+Sym(B.Max);};
    auto Eye=[&](const ERLayoutQueries::FLayoutSolids& S,FVector P){for(const auto& Start:Starts)if(S.SightClear(Start+FVector(0,0,60),P))return true;return false;};
    ERLayoutQueries::FLayoutSolids Now(L);
    struct FClimb{FVector Edge,N,At,In;double Foot=0;bool Hidden=false,Door=false;FBox Hole;};
    // A ladder on the wall below an edge point: open floor beside a route at its foot, a clear
    // climb, a standing landing on the slab and, where a wall is in the way, a doorway to cut.
    auto Climb=[&](FVector Edge,FVector N,double Top,FClimb& Out)
    {
        const FVector U(-N.Y,N.X,0),At=Edge+N*71,In=Edge-N*84;
        const int32 X=FMath::FloorToInt32((At.X-Origin.X)/Cell),Y=FMath::FloorToInt32((At.Y-Origin.Y)/Cell);
        if(X<4 || Y<4 || X>=NX-4 || Y>=NY-4 || Outside[Y*NX+X])return false;
        for(const auto* Set:{&L.InaccessibleGround,&L.WarehouseBaseCourts,&L.PoolWater})
            for(const auto& Box:*Set)if(Box.ExpandBy(FVector(100,100,10000)).IsInsideOrOn(At))return false;
        bool Near=false;for(int32 DY=-4;DY<=4;++DY)for(int32 DX=-4;DX<=4;++DX)Near|=Lane[(Y+DY)*NX+X+DX]!=0;
        for(const auto& Start:Starts)Near&=FVector::Dist2D(Start,At)>=1200;
        if(!Near)return false;
        const double Floor=Ground(L,FVector2D(At));double Foot=Floor;
        for(const auto& S:Now.Solids)
            if(S.bPawn && S.Bounds.Max.Z>Foot && S.Bounds.Max.Z<=Floor+40 && At.X>S.Bounds.Min.X-36 && At.X<S.Bounds.Max.X+36 && At.Y>S.Bounds.Min.Y-36 && At.Y<S.Bounds.Max.Y+36)Foot=S.Bounds.Max.Z;
        if(Top-Foot<240 || !Now.PawnClear(FVector(At.X,At.Y,Foot+96),FVector(At.X,At.Y,Top+96)) ||
           !Now.PawnClear(FVector(In.X,In.Y,Top+96),FVector(In.X,In.Y,Top+100)) || Now.PawnClear(FVector(In.X,In.Y,Top+96),FVector(In.X,In.Y,Top-40)) ||
           Eye(Now,FVector(At.X,At.Y,Top+150)))return false;
        // The wall behind the ladder is solid: it never stands across a doorway or a shop window.
        for(float Side:{-45.f,0.f,45.f})for(float Z:{60.f,180.f})
            if(Now.SightClear(At+U*Side+FVector(0,0,Foot+Z),Edge-N*60+U*Side+FVector(0,0,Foot+Z)))return false;
        const FBox Rails=FBox(ForceInit)+(Edge+N*11.5+U*50+FVector(0,0,Foot+.5))+(Edge+N*20.5-U*50+FVector(0,0,Top+110));
        for(const auto& S:Now.Solids)if(S.Bounds.Intersect(Rails) && S.Bounds.Overlap(Rails).GetSize().GetMin()>.5)return false;
        Out.Edge=Edge;Out.N=N;Out.At=At;Out.In=In;Out.Foot=Foot;Out.Hidden=!Lane[Y*NX+X];
        Out.Door=!Now.PawnClear(FVector(At.X,At.Y,Top+96),FVector(In.X,In.Y,Top+96));
        Out.Hole=FBox(ForceInit)+(Edge-N*14-U*55+FVector(0,0,Top+.5))+(Edge+N*11+U*55+FVector(0,0,Top+210));
        return !Out.Door || CarveFixed(L,Out.Hole,false);
    };
    int32 Uppers=0,UpperRooms=0;
    const int32 UpperBudget=FMath::Clamp(FMath::RoundToInt32(Arena.X*Arena.Y/10000/1500),1,3);
    for(int32 G=0;G<Roofs.Num() && Uppers<UpperBudget;++G)
    {
        if(Roofs[G].Group!=G)continue;
        TArray<FBox> Slabs;FBox Bounds(ForceInit);double Area=0;int32 Largest=0;
        for(const auto& Roof:Roofs)if(Roof.Group==G)
        {
            const int32 Index=Slabs.Add(Roof.Box);
            if(Roof.Box.GetSize().X*Roof.Box.GetSize().Y>Slabs[Largest].GetSize().X*Slabs[Largest].GetSize().Y)Largest=Index;
            Bounds+=Roof.Box;Area+=Roof.Box.GetSize().X*Roof.Box.GetSize().Y;
        }
        const FVector Mid=Bounds.GetCenter();const double Top=Bounds.Max.Z;
        if(Area<200000 || Mid.Y>-1)continue;
        // The mate level must exist: the same slabs under the layout's symmetry.
        bool Paired=true;
        for(const FBox& Slab:Slabs)Paired&=Roofs.ContainsByPredicate([&](const FRoof& Roof){return Roof.Box.GetCenter().Equals(Sym(Slab.GetCenter()),5) && Roof.Group!=G;});
        const FBox Reach=Bounds.ExpandBy(FVector(60,60,0));
        auto Above=[&](FVector P){return P.X>Reach.Min.X && P.X<Reach.Max.X && P.Y>Reach.Min.Y && P.Y<Reach.Max.Y && P.Z>Top-20 && P.Z<Top+220;};
        bool Reached=false;
        for(const auto& Ladder:L.Ladders)Reached|=Above(Ladder.Frame.TransformPositionNoScale(FVector(-100,0,Ladder.DeckHeight+10)));
        for(const auto* Set:{&L.Routes,&L.StairRoutes,&L.RoomAccessRoutes,&L.RooftopRoutes,&L.RoofTransfers,&L.CrossingRoutes,&L.DistrictRoutes})
            for(const auto& Path:*Set)for(const auto& P:Path)Reached|=Above(P);
        for(const auto& Step:L.Steps)Reached|=Above(Step.GetLocation()+FVector(0,0,20));
        const FVector Stand(Slabs[Largest].GetCenter().X,Slabs[Largest].GetCenter().Y,Top+96);
        if(!Paired || Reached || !Now.PawnClear(Stand,Stand+FVector(0,0,4)))continue;
        const bool Room=!Now.SightClear(Stand+FVector(0,0,30),Stand+FVector(0,0,1200));
        // An open roof is a place to stand anywhere on it: none of it may see a start.
        bool Exposed=false;
        if(!Room)for(const FBox& Slab:Slabs)for(int32 Corner=0;Corner<5 && !Exposed;++Corner)
        {
            const FVector Inset=Slab.GetExtent()-FVector(60,60,0);
            Exposed=Eye(Now,FVector(Slab.GetCenter().X+(Corner==4 ? 0:Corner&1 ? Inset.X:-Inset.X),Slab.GetCenter().Y+(Corner==4 ? 0:Corner&2 ? Inset.Y:-Inset.Y),Top+160));
        }
        if(Exposed)continue;
        bool Done=false;
        for(int32 Pass=0;Pass<2 && !Done;++Pass)for(int32 S=0;S<Slabs.Num() && !Done;++S)for(int32 E=0;E<4 && !Done;++E)for(int32 K=0;K<16 && !Done;++K)
        {
            const FBox Slab=Slabs[S];const int32 Q=(E+G)%4;
            const FVector N(Q==0 ? 1:Q==2 ? -1:0,Q==1 ? 1:Q==3 ? -1:0,0),U(-N.Y,N.X,0),C=Slab.GetCenter(),H=Slab.GetExtent();
            const double Out=FMath::Abs(N.X)*H.X+FMath::Abs(N.Y)*H.Y,Run=FMath::Abs(U.X)*H.X+FMath::Abs(U.Y)*H.Y,T=((K+1)/2)*((K%2)!=0 ? 100.:-100.);
            if(FMath::Abs(T)>Run-80)break;
            const FVector Edge=FVector(C.X,C.Y,0)+N*Out+U*T;
            if(Slabs.ContainsByPredicate([&](const FBox& Other){return Other.IsInsideXY(Edge+N*30);}))continue;
            FClimb A,B;
            if(!Climb(Edge,N,Top,A) || (Pass==0 && !A.Hidden) || !Climb(Sym(Edge),SymDir(N),Top,B))continue;
            const TArray<FERLayoutFixedPiece> Saved=L.FixedPieces;const int32 SavedStart=L.GroundFixedStart;
            if(A.Door)CarveFixed(L,A.Hole,true);
            if(B.Door)CarveFixed(L,B.Hole,true);
            TArray<FVector> Windows;
            if(Room)
            {
                // A glazed plate on a wall of this level is a window: its glass footprint is cut through
                // the wall and the plate behind it, which leaves the plate as the frame.
                TArray<FBox> Panes;TArray<FVector> Outward;
                for(int32 I=0;I<L.GroundFixedStart;++I)
                {
                    const auto& P=L.FixedPieces[I];
                    const FTransform Frame(P.Transform.GetRotation(),P.Transform.GetLocation());
                    if(P.Material!=M::Glass || P.Mesh || P.MeshCollision || !Aligned(Frame.GetRotation()))continue;
                    const FVector Half=P.Transform.GetScale3D()*50;
                    const FBox Pane=FBox(-Half,Half).TransformBy(Frame);
                    const bool AlongX=Pane.GetSize().X>Pane.GetSize().Y;
                    if(Pane.Min.Z<Top+30 || Pane.Max.Z>Top+300 || FMath::Min(Pane.GetSize().X,Pane.GetSize().Y)>12)continue;
                    for(const FBox& Storey:Slabs)if(Storey.ExpandBy(FVector(30,30,400)).Intersect(Pane))
                    {
                        const int32 Axis=AlongX ? 1:0;FVector Side=FVector::ZeroVector;Side[Axis]=Pane.GetCenter()[Axis]>Storey.GetCenter()[Axis] ? 1:-1;
                        Panes.Add(Pane);Outward.Add(Side);break;
                    }
                }
                for(int32 I=0;I<Panes.Num();++I)
                {
                    const FBox Hole=Panes[I].ExpandBy(Outward[I].GetAbs()*45),MateHole=SymBox(Hole);
                    if(Eye(Now,Panes[I].GetCenter()+Outward[I]*30) || !CarveFixed(L,Hole,false) || !CarveFixed(L,MateHole,false))continue;
                    CarveFixed(L,Hole,true);CarveFixed(L,MateHole,true);
                    Windows.Add(FVector(Panes[I].GetCenter().X,Panes[I].GetCenter().Y,Top)-Outward[I]*30);
                }
            }
            const ERLayoutQueries::FLayoutSolids After(L);
            auto Through=[&](const FClimb& Spot){return After.PawnClear(FVector(Spot.At.X,Spot.At.Y,Top+96),FVector(Spot.In.X,Spot.In.Y,Top+96));};
            if(!Through(A) || !Through(B)){L.FixedPieces=Saved;L.GroundFixedStart=SavedStart;continue;}
            for(const auto* Spot:{&A,&B})
            {
                const FTransform Frame(FRotator(0,FMath::RadiansToDegrees(FMath::Atan2(Spot->N.Y,Spot->N.X)),0),FVector(Spot->Edge.X,Spot->Edge.Y,Spot->Foot)+Spot->N*16);
                const double Deck=Top-Spot->Foot;
                auto Metal=[&](FVector Min,FVector Max)
                {
                    FERLayoutFixedPiece Piece;Piece.Material=M::Metal;
                    Piece.Transform=FTransform(Frame.GetRotation(),Frame.TransformPositionNoScale((Min+Max)*.5),(Max-Min)/100);L.FixedPieces.Add(Piece);
                };
                for(float Side:{-45.f,45.f})Metal({-4,Side-4,0},{4,Side+4,Deck+110});
                for(double Z=25;Z<Deck;Z+=25)Metal({-3,-41,Z-3},{3,41,Z+3});
                L.Ladders.Add({Frame,float(Deck+5),float(Deck)});
                // Furniture keeps clear of the foot of the ladder.
                const FVector Foot(Spot->At.X,Spot->At.Y,Spot->Foot);
                for(auto& Space:L.Interiors)
                    if(FBox(FVector(-Space.Half-FVector2D(40),-50),FVector(Space.Half+FVector2D(40),200)).IsInsideOrOn(Space.Frame.InverseTransformPositionNoScale(Foot)))Space.Doors.Add(Foot);
            }
            if(Room)for(const FBox& Part:Slabs)
            {
                const FVector SC=Part.GetCenter(),SH=Part.GetExtent();
                if(FMath::Min(SH.X,SH.Y)<150)continue;
                FERLayoutInterior Space;Space.Frame=FTransform(FVector(SC.X,SC.Y,Top));Space.Half=FVector2D(SH.X-15,SH.Y-15);
                Space.Height=280;Space.Kind=EERInteriorKind::BackRoom;Space.Walls=0;
                for(int32 W=0;W<4;++W)
                {
                    const FVector D(W==1 ? 1:W==3 ? -1:0,W==0 ? -1:W==2 ? 1:0,0);
                    const double Far=FMath::Abs(D.X)*SH.X+FMath::Abs(D.Y)*SH.Y;
                    if(!After.PawnClear(FVector(SC.X,SC.Y,Top+96),FVector(SC.X,SC.Y,Top+96)+D*(Far+40)))Space.Walls|=uint8(1<<W);
                    else Space.Doors.Add(FVector(SC.X,SC.Y,Top)+D*Far);
                }
                Space.Doors.Add(FVector(A.In.X,A.In.Y,Top));Space.Doors.Add(FVector(A.Edge.X,A.Edge.Y,Top));
                for(const auto& Window:Windows)if(Part.ExpandBy(FVector(40,40,400)).IsInsideOrOn(Window))Space.Doors.Add(Window);
                AddInteriorPair(L,MoveTemp(Space),Mirror);
            }
            L.FeatureNames.AddUnique(Room ? TEXT("Ground.UpperRoom"):TEXT("Ground.RoofAccess"));
            ++L.GroundFeatures;++Uppers;UpperRooms+=Room ? 1:0;Done=true;
            Now=ERLayoutQueries::FLayoutSolids(L);
        }
    }
    UE_LOG(LogTemp,Display,TEXT("ER_OPEN_GROUND seed=%d grid=%dx%d pairs=%d/%d lookouts=%d compounds=%d terraces=%d boulders=%d breaches=%d uppers=%d rooms=%d spots=%d widest_m=%d crowded=%d steep=%d"),Seed,NX,NY,Pairs,Budget,Lookouts,Compounds,Terraces,Boulders,Breaches,Uppers,UpperRooms,Spots,Widest,Crowded,Steep);
}
}
