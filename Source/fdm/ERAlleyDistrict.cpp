#include "ERProceduralMap.h"
#include "Engine/StaticMesh.h"
#include "Algo/Reverse.h"

namespace
{
struct FPlot
{
    FVector2D Min,Max;
    bool Contains(FVector2D P) const{return P.X>Min.X && P.X<Max.X && P.Y>Min.Y && P.Y<Max.Y;}
};
// Exact rectangular complement of the streets. Bounded coordinate compression
// avoids random packing, disconnected slivers and seed-dependent retries.
TArray<FPlot> Lots(const FPlot& Extent,const TArray<FPlot>& Streets)
{
    TArray<double> X{Extent.Min.X,Extent.Max.X},Y{Extent.Min.Y,Extent.Max.Y};
    for(const auto& S:Streets)
    {
        X.AddUnique(FMath::Clamp(S.Min.X,Extent.Min.X,Extent.Max.X));X.AddUnique(FMath::Clamp(S.Max.X,Extent.Min.X,Extent.Max.X));
        Y.AddUnique(FMath::Clamp(S.Min.Y,Extent.Min.Y,Extent.Max.Y));Y.AddUnique(FMath::Clamp(S.Max.Y,Extent.Min.Y,Extent.Max.Y));
    }
    X.Sort();Y.Sort();TArray<FPlot> Out;if(X.Num()>220 || Y.Num()>160)return Out;
    const int32 NX=X.Num()-1,NY=Y.Num()-1;TArray<uint8> Grid;Grid.Init(1,NX*NY);
    for(int32 J=0;J<NY;++J)for(int32 I=0;I<NX;++I)
        for(const auto& S:Streets)if(S.Contains({(X[I]+X[I+1])*.5,(Y[J]+Y[J+1])*.5})){Grid[J*NX+I]=0;break;}
    for(int32 J=0;J<NY;++J)for(int32 I=0;I<NX;++I)if(Grid[J*NX+I])
    {
        int32 End=I+1;while(End<NX && Grid[J*NX+End])++End;
        int32 Top=J+1;for(;Top<NY;++Top){bool Full=true;for(int32 K=I;K<End;++K)Full&=Grid[Top*NX+K]!=0;if(!Full)break;}
        for(int32 V=J;V<Top;++V)for(int32 U=I;U<End;++U)Grid[V*NX+U]=0;
        if(X[End]-X[I]>=2 && Y[Top]-Y[J]>=2)Out.Add({{X[I],Y[J]},{X[End],Y[Top]}});
    }
    return Out;
}
FVector Opposite(FVector P){return {-P.X,-P.Y,P.Z};}
FPlot Opposite(FPlot P){return {-P.Max,-P.Min};}
}

namespace ERProcedural
{
bool BuildAlleyDistrict(const UERMapTemplate& T,int32 Seed,TArray<FERMapTemplatePiece>& Pieces,FERProceduralLayout& L,TArray<FVector>& Patrol)
{
    const auto& V=T.Town;
    if(T.Symmetry!=EERTemplateSymmetry::None || !V.TraversableTown || V.MinAccesses<2 || V.MaxAccesses>4 || V.MinAccesses>V.MaxAccesses ||
       V.MinCentralWalls<0 || V.MaxCentralWalls>2 || V.MinCentralWalls>V.MaxCentralWalls ||
       V.ArenaMin.ContainsNaN() || V.ArenaMax.ContainsNaN() || V.ArenaMin.X<(T.SizingTeamSize>0 ? 3200:7600) || V.ArenaMin.Y<(T.SizingTeamSize>0 ? 3200:8400) ||
       V.ArenaMax.X>(T.SizingTeamSize>0 ? 20000:9500) || V.ArenaMax.Y>(T.SizingTeamSize>0 ? 24000:11000) || V.ArenaMin.X>V.ArenaMax.X || V.ArenaMin.Y>V.ArenaMax.Y ||
       !FMath::IsFinite(V.AxisWarp) || V.AxisWarp<0 || V.AxisWarp>3 || V.BuildingHeight.ContainsNaN() ||
       V.BuildingHeight.X<400 || V.BuildingHeight.Y>800 || V.BuildingHeight.X>V.BuildingHeight.Y ||
       !V.WallSurface || !V.RoofSurface || !V.GroundSurface || !V.IronSurface || !V.ArchMesh ||
       (V.ProceduralHouses ? !V.DestructibleArchitecture:V.BuildingMeshes.IsEmpty()) || V.BuildingMeshes.Num()>16)return false;
    for(const auto& Mesh:V.BuildingMeshes)if(!Mesh || Mesh->GetBoundingBox().GetSize().GetMin()<1)return false;
    FRandomStream R(Seed),Art(int32(HashCombine(uint32(Seed),0x44495354u)));
    auto Pick=[&](float A,float B,float Grid=50.f){return R.RandRange(FMath::CeilToInt(A/Grid),FMath::FloorToInt(B/Grid))*Grid;};
    L.SeededDistrict=true;L.GradedTerrain=true;L.GroundSurface=V.GroundSurface;
    L.ArenaSize={Pick(V.ArenaMin.X,V.ArenaMax.X),Pick(V.ArenaMin.Y,V.ArenaMax.Y),T.ArenaSize.Z};
    const FVector Half=L.ArenaSize*.5;
    L.DistrictAccesses=V.MinAccesses+uint32(Seed)%uint32(V.MaxAccesses-V.MinAccesses+1);
    L.DistrictBends=R.RandRange(0,T.SizingTeamSize>0 && T.SizingTeamSize<=2 ? 1:3);
    // Independent of lane count and art: all nine wall/access combinations
    // occur, and changing the allowed wall range does not reroll the town.
    L.DistrictWallCount=V.MinCentralWalls+((uint32(Seed)>>4)^uint32(Seed))%uint32(V.MaxCentralWalls-V.MinCentralWalls+1);
    FRandomStream Center(int32(HashCombine(uint32(Seed),0x434f5245u)));
    L.DistrictWallOffset=L.DistrictWallCount==2 ? Center.RandRange(26,32)*50.f:0;
    const bool DenseCenter=T.SizingTeamSize==3;
    L.DistrictOpenHalfDepth=L.DistrictWallCount==2 ? L.DistrictWallOffset-120:L.DistrictWallCount==0 ? Center.RandRange(14,22)*50.f:0;
    if(DenseCenter)L.DistrictOpenHalfDepth=0;
    const float DistrictEnd=DenseCenter ? -220:L.DistrictWallCount==2 ? -L.DistrictWallOffset-180:-FMath::Max(220.f,L.DistrictOpenHalfDepth);
    Pieces.Reset();Patrol.Reset();L.AlphaSpawns.Reset();L.OmegaSpawns.Reset();
    auto Box=[&](FVector P,FVector S,UMaterialInterface* Surface,bool Walk=false,float Yaw=0)->FERMapTemplatePiece&
    {
        auto& B=Pieces.AddDefaulted_GetRef();B.Center=P;B.Size=S;B.FixedSurface=Surface;
        B.Destructible=false;B.Material=EERBlockMaterial::Stone;B.WalkableSurface=Walk;B.RotationYaw=Yaw;return B;
    };
    auto PairBox=[&](FVector P,FVector S,UMaterialInterface* Surface,bool Walk=false,float Yaw=0)
    {Box(P,S,Surface,Walk,Yaw);Box(Opposite(P),S,Surface,Walk,Yaw+180);};
    auto Pad=[&](FPlot P,float Z)
    {for(const auto& F:{P,Opposite(P)})L.TerrainPads.Add(FBox(FVector(F.Min,Z),FVector(F.Max,Z)));};
    TArray<FPlot> Roads;
    auto Space=[&](FPlot P){Roads.Add(P);Roads.Add(Opposite(P));};
    auto Street=[&](const TArray<FVector>& Path,float W)
    {
        for(int32 I=1;I<Path.Num();++I)
        {
            const auto A=Path[I-1],B=Path[I];
            Space({{FMath::Min(A.X,B.X)-W*.5f,FMath::Min(A.Y,B.Y)-W*.5f},
                   {FMath::Max(A.X,B.X)+W*.5f,FMath::Max(A.Y,B.Y)+W*.5f}});
        }
    };
    // Compact 3v3 districts build into the core; other profiles retain their
    // reserved square. A paired cross alley connects the central approaches.
    if(L.DistrictOpenHalfDepth>0)
    {
        Space({{-Half.X+30,-L.DistrictOpenHalfDepth},{Half.X-30,L.DistrictOpenHalfDepth}});
        Pad({{-Half.X,-L.DistrictOpenHalfDepth},{Half.X,L.DistrictOpenHalfDepth}},0);
    }
    if(DenseCenter)
    {
        for(int32 Wall=0;Wall<L.DistrictWallCount;++Wall)
        {
            const float WallY=L.DistrictWallCount==1 ? 0:(Wall ? 1.f:-1.f)*L.DistrictWallOffset;
            Space({{-Half.X,WallY-180},{Half.X,WallY+180}});
        }
    }
    const float SpawnDepth=T.SizingTeamSize==1 ? 600:T.SizingTeamSize>0 ? FMath::GridSnap(FMath::Clamp(float(Half.Y)*.86f-2300.f,800.f,1500.f),100.f):1500.f;
    const float BaseOffset=T.SizingTeamSize==1 ? 350:T.SizingTeamSize>0 ? FMath::GridSnap(FMath::Max(SpawnDepth*.5f+100,FMath::Min(1050.f,float(Half.Y)*.43f-SpawnDepth*.5f-100)),50.f):1050.f;
    const FVector Base(Pick(-400,400),-Half.Y+BaseOffset,0);
    const FVector2D SpawnSize(Pick(T.SizingTeamSize==1 ? 1400:2000,T.SizingTeamSize==1 ? 1600:2500),SpawnDepth);
    Space({FVector2D(Base)-SpawnSize*.5,FVector2D(Base)+SpawnSize*.5});
    for(const auto C:{Base,Opposite(Base)})L.TownCourts.Add(FBox(C-FVector(SpawnSize*.5,0),C+FVector(SpawnSize*.5,1)));
    TArray<TArray<FVector>> North;TArray<float> GateX,Widths;
    const float Spread=Pick(Half.X*(L.DistrictAccesses==2 ? .35f:.59f),Half.X*(L.DistrictAccesses==2 ? .50f:.70f)),FirstY=-Half.Y*.60f,LastY=FMath::Min3(float(-Half.Y*.16f),DistrictEnd-200,V.LowCoverAndUnderpass ? -FMath::Min(1350.f,float(Half.Y)*.35f):0.f);
    // Corresponding routes may have different silhouettes, but every ground
    // junction, rise, plaza, roof stair and tower has an exact half-turn mate.
    for(int32 Lane=0;Lane<L.DistrictAccesses;++Lane)
    {
        const float X=(float(Lane)/(L.DistrictAccesses-1)*2-1)*Spread;
        const float W=Lane>=(L.DistrictAccesses+1)/2 ? Widths[L.DistrictAccesses-1-Lane]:Pick(340,480,20);
        const float Band=2*Spread/(L.DistrictAccesses-1),Bend=FMath::Min(650.f,Band*.22f);
        const float Limit=L.DistrictAccesses==2 ? Half.X-(T.SizingTeamSize>0 && T.SizingTeamSize<=2 ? 700:1550):Half.X-(V.LowCoverAndUnderpass ? 1050:500);
        const float Entry=FMath::Clamp(X+Pick(-Bend,Bend),-Limit,Limit),First=FirstY-Pick(0,250);
        TArray<FVector> Path{Base,FVector(Entry,Base.Y,0),FVector(Entry,First,0)};
        float AtX=Entry;
        for(int32 Turn=0;Turn<L.DistrictBends+1;++Turn)
        {
            const float Y=FMath::Lerp(First,LastY,float(Turn)/(L.DistrictBends+1));
            const float Next=FMath::Clamp(X+Pick(-Bend,Bend),-Limit,Limit);
            if(FMath::Abs(Next-AtX)>100){Path.Add({AtX,Y,0});Path.Add({Next,Y,0});AtX=Next;}
            const float NextY=FMath::Lerp(First,LastY,float(Turn+1)/(L.DistrictBends+1));Path.Add({AtX,NextY,0});
        }
        Path.Add({X,LastY,0});
        if(DenseCenter)
        {
            // Offset each central approach independently, returning to its arch
            // only at the wall. No straight lane cuts the entire district.
            const float Offset=Pick(200,FMath::Max(200.f,FMath::Min(400.f,Band*.22f)))*(Lane%2==0 ? 1.f:-1.f);
            const float TurnY=Pick(-1000,-850),ReturnY=Pick(-450,-300);
            Path.Add({X,TurnY,0});Path.Add({X+Offset,TurnY,0});
            Path.Add({X+Offset,ReturnY,0});Path.Add({X,ReturnY,0});
        }
        Path.Add({X,0,0});
        // Remove coincident waypoints before validation and actual movement.
        for(int32 I=Path.Num()-1;I>0;--I)if(FVector::Dist2D(Path[I],Path[I-1])<1)Path.RemoveAt(I);
        Street(Path,W);North.Add(Path);GateX.Add(X);Widths.Add(W);
        L.TownStreetNames.Add(FName(*FString::Printf(TEXT("District_%d"),Lane)));
        L.GroundRoutes.Add({{Entry,Base.Y-100,100},{Entry,FMath::Lerp(Base.Y,First,.4f),100},{Entry,First+100,100}});
    }
    if(DenseCenter)
    {
        // Each gap gets a different cross-street level and an off-center elbow.
        // Reserve the same polyline used by traversal checks, then pair it.
        auto LanePoint=[&](int32 Lane,float Y)
        {
            const auto& Path=North[Lane];
            for(int32 I=Path.Num()-1;I>0;--I)
                if(FMath::IsNearlyEqual(Path[I-1].X,Path[I].X) && Y>=Path[I-1].Y && Y<=Path[I].Y)
                    return FVector(Path[I].X,Y,100);
            return FVector(GateX[Lane],Y,100);
        };
        TArray<FVector> Cross;float CrossY=Pick(-1050,-450);
        Cross.Add(LanePoint(0,CrossY));
        for(int32 Lane=1;Lane<GateX.Num();++Lane)
        {
            const float NextY=CrossY<-750 ? Pick(-500,-400):Pick(-1200,-1100);
            const FVector Join=LanePoint(Lane,NextY);
            const float Elbow=FMath::Lerp(float(Cross.Last().X),float(Join.X),Pick(35,65,5)/100.f);
            Cross.Add({Elbow,CrossY,100});Cross.Add({Elbow,NextY,100});Cross.Add(Join);
            CrossY=NextY;
        }
        Street(Cross,300);L.RoomAccessRoutes.Add(Cross);
        TArray<FVector> CrossMate;for(const auto& Point:Cross)CrossMate.Add(Opposite(Point));
        L.RoomAccessRoutes.Add(CrossMate);
    }
    // Reserve both climb aprons before house lots are partitioned.
    for(int32 Wall=0;Wall<L.DistrictWallCount;++Wall)
    {
        const float Y=L.DistrictWallCount==1 ? 0:(Wall ? 1.f:-1.f)*L.DistrictWallOffset;
        for(float X:{-float(Half.X)+400,float(Half.X)-400})
            Space({{X-350,Y-1100},{X+350,Y+1100}});
    }
    L.GroundRoutes.SetNum(1);TArray<FVector> SouthGrade;for(auto P:L.GroundRoutes[0])SouthGrade.Add(Opposite(P));L.GroundRoutes.Add(SouthGrade);
    // Cross streets create loops within each team's district.
    const float LinkY=Pick(-Half.Y*.67f,-Half.Y*.62f);
    Street({{GateX[0],LinkY,0},{GateX.Last(),LinkY,0}},Pick(340,440,20));
    L.RoomAccessRoutes.Add({{GateX[0],LinkY,100},{GateX.Last(),LinkY,100}});
    L.RoomAccessRoutes.Add({Opposite({GateX[0],LinkY,100}),Opposite({GateX.Last(),LinkY,100})});
    // A broad, paired market square contains the raised crossing. Additional
    // courtyards vary in size and rectangular/L/cross shape between seeds.
    const int32 PlazaLane=L.DistrictAccesses==2 ? R.RandRange(0,1):R.RandRange(1,L.DistrictAccesses-2),PlazaPairs=R.RandRange(1,T.SizingTeamSize>0 && T.SizingTeamSize<=2 ? 1:2);
    TArray<FVector> PlazaCenters;TArray<FVector2D> PlazaSizes;
    for(int32 I=0;I<PlazaPairs;++I)
    {
        const int32 Lane=(PlazaLane+I)%North.Num();const auto& Path=North[Lane];
        float Y=I ? FirstY-100:T.SizingTeamSize==1 ? 0:DenseCenter ? -1200:-Pick(350,450),X=GateX[Lane];
        if(!I)
        {
            // The final straight approaches have guaranteed separation for
            // the bridge stairs; winding streets farther back stay untouched.
            X=GateX[Lane];
        }
        for(int32 J=1;J<Path.Num();++J)if(FMath::IsNearlyEqual(Path[J-1].X,Path[J].X) && Y>=Path[J-1].Y && Y<=Path[J].Y){X=Path[J].X;break;}
        const FVector2D S=I ? FVector2D(Pick(1500,2200),Pick(1500,2200)):(T.SizingTeamSize==1 ? FVector2D(1400,1000):T.SizingTeamSize==2 ? FVector2D(2600,1200):DenseCenter ? FVector2D(2400,1100):FVector2D(2800,2300));
        X=FMath::Clamp(X,-Half.X+S.X*.5f+100,Half.X-S.X*.5f-100);
        FVector C(X,Y,0);PlazaCenters.Add(C);PlazaSizes.Add(S);
        const FPlot Square{FVector2D(C)-S*.5f,FVector2D(C)+S*.5f};
        if(I && R.RandRange(0,1))
        {Space({Square.Min,{Square.Max.X,Square.Min.Y+S.Y*.65f}});Space({{Square.Min.X,Square.Min.Y+S.Y*.65f},{Square.Min.X+S.X*.6f,Square.Max.Y}});}
        else Space(Square);
        // Link the square to its intended lane even when clamped by the edge.
        Street({{GateX[Lane],Y,0},C},Widths[Lane]);
        const float Z=AlleyBaseHeight(L.ArenaSize,FVector2D(C),true);if(!I)Pad(Square,Z);
        for(const auto Q:{C,Opposite(C)})L.TownCourts.Add(FBox(Q-FVector(S*.5f,0)+FVector(0,0,Z),Q+FVector(S*.5f,1)+FVector(0,0,Z)));
    }
    if(!PlanAlleyUnderpass(T,Seed,Base,Pieces,L))return false;
    // Breachable road stays clear of the bridge stairs and piers, else no blast reaches it.
    if(T.SizingTeamSize!=1)for(const auto& Hole:L.UnderpassBreaches)
    {
        const FVector2D Gap=(FVector2D(Hole.GetCenter())-FVector2D(PlazaCenters[0])).GetAbs(),Extent=FVector2D(Hole.GetExtent())+FVector2D(1300,200);
        if(Gap.X<Extent.X && Gap.Y<Extent.Y){UE_LOG(LogTemp,Warning,TEXT("ER_DISTRICT_REJECT seed=%d breach_under_bridge=1"),Seed);return false;}
    }
    for(const auto& Ramp:L.UnderpassRamps)Space({FVector2D(Ramp.Bounds.Min)-FVector2D(100),FVector2D(Ramp.Bounds.Max)+FVector2D(100)});
    for(const auto& Hole:L.UnderpassBreaches)
    {const FPlot P{FVector2D(Hole.Min)-FVector2D(120),FVector2D(Hole.Max)+FVector2D(120)};Space(P);Pad(P,0);}
    if(!L.UnderpassApproach.IsEmpty())Street(L.UnderpassApproach,360);
    // Towers occupy open market corners, outside the circulation paths. A
    // reserved apron keeps the climb approach clear of neighboring houses.
    const int32 TowerPairs=T.SizingTeamSize>0 && T.SizingTeamSize<=2 ? 0:PlazaPairs;TArray<FPlot> TowerFootprints;
    for(int32 I=0;I<TowerPairs;++I)
    {
        const auto C=PlazaCenters[I];const float W=I ? 500:600,D=W,H=Pick(700,900,100);
        FVector Origin;bool Placed=false;float BestSite=MAX_flt;
        for(float Y=-Half.Y+550;Y< -700;Y+=300)for(float X=-Half.X+550;X<Half.X-550;X+=300)
        {
            const FVector Candidate(X,Y,0);const float Score=FVector::DistSquared2D(Candidate,C+FVector(800,650,0));if(Score>=BestSite)continue;
            const FBox Bounds(Candidate-FVector(W*.5f+140,D*.5f+140,10000),Candidate+FVector(W*.5f+140,D*.5f+140,10000));
            bool Clear=Bounds.Min.X> -Half.X+30 && Bounds.Max.X<Half.X-30 && Bounds.Min.Y> -Half.Y+30 && Bounds.Max.Y<FMath::Min(-250.f,DistrictEnd);
            for(int32 Separator=0;Separator<L.DistrictWallCount;++Separator)
            {
                const float WallY=L.DistrictWallCount==1 ? 0:(Separator ? 1.f:-1.f)*L.DistrictWallOffset;
                for(float SideX:{-float(Half.X)+400,float(Half.X)-400})
                    Clear&=!Bounds.Intersect(FBox(FVector(SideX-350,WallY-1100,-10000),FVector(SideX+350,WallY+1100,10000)));
            }
            for(const auto& Path:North)for(int32 J=1;J<Path.Num();++J)Clear&=!FMath::LineBoxIntersection(Bounds,Path[J-1],Path[J],Path[J]-Path[J-1]);
            for(const auto& Path:L.RoomAccessRoutes)for(int32 J=1;J<Path.Num();++J)Clear&=!FMath::LineBoxIntersection(Bounds,Path[J-1],Path[J],Path[J]-Path[J-1]);
            const FBox Spawn(Base-FVector(SpawnSize*.5f,10000),Base+FVector(SpawnSize*.5f,10000));Clear&=!Bounds.Intersect(Spawn);
            const auto BridgeCenter=PlazaCenters[0];Clear&=!Bounds.Intersect(FBox(BridgeCenter-FVector(1300,200,10000),BridgeCenter+FVector(1300,200,10000)));
            for(const auto& Ramp:L.UnderpassRamps)Clear&=!Bounds.Intersect(FBox(FVector(Ramp.Bounds.Min.X-100,Ramp.Bounds.Min.Y-100,-10000),FVector(Ramp.Bounds.Max.X+100,Ramp.Bounds.Max.Y+100,10000)));
            for(const auto& Hole:L.UnderpassBreaches)Clear&=!Bounds.Intersect(FBox(FVector(Hole.Min.X-120,Hole.Min.Y-120,-10000),FVector(Hole.Max.X+120,Hole.Max.Y+120,10000)));
            if(!L.UnderpassApproach.IsEmpty())for(int32 J=1;J<L.UnderpassApproach.Num();++J)Clear&=!FMath::LineBoxIntersection(Bounds,L.UnderpassApproach[J-1],L.UnderpassApproach[J],L.UnderpassApproach[J]-L.UnderpassApproach[J-1]);
            for(int32 Segment=1;Segment<L.UnderpassRoute.Num();++Segment)
                Clear&=!FMath::LineBoxIntersection(Bounds,L.UnderpassRoute[Segment-1],L.UnderpassRoute[Segment],L.UnderpassRoute[Segment]-L.UnderpassRoute[Segment-1]);
            for(const auto& Foot:TowerFootprints)Clear&=!Bounds.Intersect(FBox(FVector(Foot.Min,-10000),FVector(Foot.Max,10000)));
            if(Clear){Origin=Candidate;Placed=true;BestSite=Score;}
        }
        if(!Placed){UE_LOG(LogTemp,Warning,TEXT("ER_DISTRICT_REJECT seed=%d tower=%d"),Seed,I);return false;}
        const float Z=AlleyBaseHeight(L.ArenaSize,FVector2D(Origin),true);
        const FPlot Apron{FVector2D(Origin)-FVector2D(W*.5+160,D*.5+160),FVector2D(Origin)+FVector2D(W*.5+160,D*.5+160)};
        Space(Apron);Pad(Apron,Z);TowerFootprints.Add(Apron);
        const auto Square=PlazaSizes[I];const FVector Join=!I && L.DistrictWallCount==2 ? FVector(GateX[PlazaLane],FMath::Min(float(Origin.Y),LastY),0):FVector(FMath::Clamp(Origin.X,C.X-Square.X*.5f,C.X+Square.X*.5f),FMath::Clamp(Origin.Y,C.Y-Square.Y*.5f,C.Y+Square.Y*.5f),0);
        Street({Join,{Origin.X,Join.Y,0},Origin},320);
        const FTransform Frame(FRotator(0,180,0),Origin+FVector(0,0,Z));
        // Light brick towers use the same calibrated cells as every other
        // structure. Only ground rows anchor; the deck falls with its supports.
        auto Solid=[&](FVector P,FVector S,bool Walk=false)
        {
            for(int32 Copy=0;Copy<2;++Copy)
            {
                const FVector Center=Frame.TransformPosition(P);
                auto& B=Box(Copy ? Opposite(Center):Center,S,nullptr,Walk,Copy ? 0:180);
                B.Destructible=true;B.Material=EERBlockMaterial::Brick;
                B.Anchored=P.Z-S.Z*.5f<=.01f;
            }
        };
        const float Thickness=10,WallH=H-Thickness;
        for(float X:{-W*.5f+Thickness*.5f,W*.5f-Thickness*.5f})Solid({X,0,WallH*.5f},{Thickness,D-2*Thickness,WallH});
        Solid({0,D*.5f-Thickness*.5f,WallH*.5f},{W,Thickness,WallH});
        for(float Sign:{-1.f,1.f})Solid({Sign*(W+180)*.25f,-D*.5f+Thickness*.5f,WallH*.5f},{(W-180)*.5f,Thickness,WallH});
        Solid({0,-D*.5f+Thickness*.5f,(240+WallH)*.5f},{180,Thickness,WallH-240});
        Solid({0,0,H-Thickness*.5f},{W+40,D+40,Thickness},true);
        // Notched parapet lets the existing ladder mechanic land on the deck.
        Solid({-W*.5f-15,0,H+40},{Thickness,D+40,80});
        for(float Sign:{-1.f,1.f})
        {
            Solid({0,Sign*(D*.5f+15),H+40},{W,Thickness,80});
            Solid({W*.5f+15,Sign*(D+180)*.25f,H+40},{Thickness,(D-100)*.5f,80});
        }
        const FTransform LadderFrame(Frame.GetRotation(),Frame.TransformPosition({W*.5f+35,0,0}));
        auto Metal=[&](FVector P,FVector S)
        {
            for(int32 Copy=0;Copy<2;++Copy)
            {
                const auto Pair=Copy ? LadderFrame*FTransform(FRotator(0,180,0)):LadderFrame;
                auto& B=Box(Pair.TransformPosition(P),S,nullptr,false,Pair.Rotator().Yaw);
                B.Destructible=true;B.FixedSurface=nullptr;B.Material=EERBlockMaterial::Metal;B.Anchored=P.Z-S.Z*.5f<.01f;B.CellSurface=V.IronSurface;
            }
        };
        for(float Y:{-42.f,42.f})
        {
            for(float X:{-3.9f,3.9f})Metal({X,Y,(H+110)*.5f},{.2f,8,H+110});
            for(float Edge:{-3.9f,3.9f})Metal({0,Y+Edge,(H+110)*.5f},{7.6f,.2f,H+110});
        }
        for(float Height=25;Height<H;Height+=25)Metal({0,0,Height},{.2f,76,4});
        for(int32 Copy=0;Copy<2;++Copy)
        {
            const auto Pair=Copy ? Frame*FTransform(FRotator(0,180,0)):Frame;
            const auto Ladder=Copy ? LadderFrame*FTransform(FRotator(0,180,0)):LadderFrame;
            L.DistrictTowers.Add(Ladder);L.DistrictTowerHeights.Add(H);L.Ladders.Add({Ladder,H+110,H});
            L.TownBuildings.Add(FBox(FVector(-W*.5,-D*.5,0),FVector(W*.5,D*.5,H)).TransformBy(Pair));
        }
    }
    auto Plots=Lots({{-Half.X+30,-Half.Y+30},{Half.X-30,DistrictEnd}},Roads);
    if(Plots.IsEmpty() || Plots.Num()>180){UE_LOG(LogTemp,Warning,TEXT("ER_DISTRICT_REJECT seed=%d plots=%d"),Seed,Plots.Num());return false;}
    TArray<FPlot> Houses;const float Frontage=Pick(950,1200);
    for(const auto& P:Plots)
    {
        const auto S=P.Max-P.Min;
        const float LotFrontage=DenseCenter && P.Max.Y>-1700 ? Pick(650,1200):Frontage;
        const int32 NX=FMath::Max(1,FMath::CeilToInt(S.X/LotFrontage)),NY=FMath::Max(1,FMath::CeilToInt(S.Y/LotFrontage));
        // Unequal plot divisions avoid identical house widths along a block.
        TArray<double> Cuts{0.};
        for(int32 X=1;X<NX;++X)Cuts.Add((X+(DenseCenter ? Pick(-20,20,5)/100.f:0.f))/NX);
        Cuts.Add(1.);
        for(int32 Y=0;Y<NY;++Y)for(int32 X=0;X<NX;++X)
            Houses.Add({P.Min+FVector2D(S.X*Cuts[X],S.Y*Y/NY),P.Min+FVector2D(S.X*Cuts[X+1],S.Y*(Y+1)/NY)});
    }
    // More arena area must not multiply dense5cm house volumes without bound.
    // Spread at most20 paired houses over the larger districts; unused lots
    // become open yards. All streets, aprons and architectural dimensions stay.
    if(T.SizingTeamSize>4 && Houses.Num()>20)
    {
        TArray<FPlot> Eligible,Chosen;
        for(const auto& Lot:Houses)if((Lot.Max-Lot.Min).GetMin()>=240)Eligible.Add(Lot);
        const int32 Keep=FMath::Min(20,Eligible.Num());
        for(int32 I=0;I<Keep;++I)Chosen.Add(Eligible[I*Eligible.Num()/Keep]);
        Houses=MoveTemp(Chosen);
    }
    int32 StairPairs=0;TArray<FVector> StairCenters;
    for(const auto& Lot:Houses)
    {
        const FVector2D S=Lot.Max-Lot.Min,C=(Lot.Min+Lot.Max)*.5f;const float Z=AlleyBaseHeight(L.ArenaSize,C,true);
        Pad(Lot,Z);float Height=Pick(V.BuildingHeight.X,T.SizingTeamSize>4 ? FMath::Min(600.f,float(V.BuildingHeight.Y)):V.BuildingHeight.Y);
        const bool HasHouse=S.GetMin()>=240;bool Access=false;FTransform Frame;float W=0,D=0;
        FVector2D HouseCenter=C,HouseSize=S;float Yaw=Art.RandRange(0,3)*90;
        bool Distant=true;for(auto P:StairCenters)Distant&=FVector::Dist2D(P,FVector(C,0))>1700;
        if(HasHouse && StairPairs<2 && Distant)for(int32 Side=0;Side<4;++Side)
        {
            const bool Swap=Side%2!=0;W=Swap ? S.Y:S.X;D=Swap ? S.X:S.Y;if(W<500 || D<620)continue;
            Frame=FTransform(FRotator(0,Side*90,0),FVector(C,Z));
            const auto Entry=Frame.TransformPosition({-W*.5f+70,-D*.5f-60,0}),SideEntry=Frame.TransformPosition({-W*.5f-60,-D*.5f+90,0});bool StreetSide=false;
            for(const auto& Road:Roads)StreetSide|=Road.Contains(FVector2D(Entry)) || Road.Contains(FVector2D(SideEntry));if(!StreetSide)continue;
            Access=true;++StairPairs;StairCenters.Add(FVector(C,0));Yaw=Side*90;
            Height=FMath::Min3(Height,600.f,float(FMath::FloorToInt((D-320)/30))*40);
            HouseCenter=FVector2D(Frame.TransformPosition({150,0,0}));HouseSize=Swap ? FVector2D(D,W-300):FVector2D(W-300,D);break;
        }
        if(HasHouse)
        {
            auto* Mesh=V.ProceduralHouses ? nullptr:V.BuildingMeshes[Art.RandRange(0,V.BuildingMeshes.Num()-1)].Get();
            for(int32 Copy=0;Copy<2;++Copy)
            {
                const FVector P(HouseCenter,Z);
                const bool Swap=int32(Yaw/90)%2!=0;
                if(V.ProceduralHouses)
                {
                    const FTransform Module(FRotator(0,Yaw+Copy*180,0),Copy ? Opposite(P):P);
                    const FVector Size(Swap ? HouseSize.Y:HouseSize.X,Swap ? HouseSize.X:HouseSize.Y,Height);
                    TArray<int32> StreetFaces;
                    for(int32 Face=0;Face<4;++Face)
                    {
                        bool Clear=true;
                        for(float Distance:{40.f,85.f,130.f})for(float Along:{-80.f,0.f,80.f})
                        {
                            FVector Outside=FVector::ZeroVector;Outside[Face/2]=(Face%2 ? 1:-1)*(Size[Face/2]*.5+Distance);Outside[1-Face/2]=Along;
                            const auto Point=FVector2D(Module.TransformPosition(Outside));
                            Clear&=Roads.ContainsByPredicate([&](const auto& Road){return Road.Contains(Point);});
                        }
                        if(Clear)StreetFaces.Add(Face);
                    }
                    AddAlleyHouseModule(T,Module,Size,Access,StreetFaces,L);continue;
                }
                auto& B=Box((Copy ? Opposite(P):P)+FVector(0,0,Height*.5f),FVector(HouseSize,Height),nullptr);
                B.FixedMesh=Mesh;B.MeshCollision=true;B.MeshRotation.Yaw=Yaw+Copy*180;
                L.Houses.Add({FTransform(FRotator(0,Yaw+Copy*180,0),Copy ? Opposite(P):P,FVector(Swap ? HouseSize.Y:HouseSize.X,Swap ? HouseSize.X:HouseSize.Y,Height)),Mesh,Access});
            }
        }
        else PairBox(FVector(C,Z+Height*.5f),FVector(S,Height),V.WallSurface);
        for(const auto& Foot:{Lot,Opposite(Lot)})L.TownBuildings.Add(FBox(FVector(Foot.Min,Z),FVector(Foot.Max,Z+Height)));
        if(Access)
        {
            const int32 N=FMath::CeilToInt(Height/40);const float Rise=Height/(2*N),Run=N*30.f;
            auto P=[&](FVector V){return Frame.TransformPosition(V+FVector(-W*.5f,-D*.5f,0));};
            auto Solid=[&](FVector C,FVector Size){PairBox(P(C),Size,V.RoofSurface,true,Yaw);};
            TArray<FVector> Route{P({70,90,100}),P({70,180,100})};
            for(int32 I=0;I<N;++I){const float H=(I+1)*Rise;if(I+1<N)Solid({70,200+(I+.5f)*30,H*.5f},{120,30,H});Route.Add(P({70,200+(I+.5f)*30,H+100}));}
            // Fill the rear stair bay to the lot edge. A player dropping from
            // the roof lands on the switchback and can descend to the street.
            const float LandingStart=200+Run-30;
            Solid({150,(LandingStart+D)*.5f,Height*.25f},{300,D-LandingStart,Height*.5f});
            Route.Add(P({70,200+Run+60,Height*.5f+100}));Route.Add(P({230,200+Run+60,Height*.5f+100}));
            for(int32 I=0;I<N;++I)
            {
                const float H=(N+I+1)*Rise;
                const float Y=200+Run-(I+.5f)*30;
                // The first upper tread sits on the landing, which also forms the last lower tread.
                const float Bottom=I==0 ? Height*.5f:0.f;
                Solid({230,Y,(H+Bottom)*.5f},{120,30,H-Bottom});
                Route.Add(P({230,Y,H+100}));
            }
            const float Far=300+(W-300)*.15f;
            // The procedural roof starts at x=300 in the stair frame.
            const float LandingEnd=V.ProceduralHouses ? 300.f:Far;
            Solid({(170+LandingEnd)*.5f,140,Height-10},{LandingEnd-170,120,20});
            Route.Add(P({230,140,Height+100}));Route.Add(P({Far,140,Height+100}));Route.Add(P({Far+50,140,(V.ProceduralHouses ? Height:Height*.94f)+100}));
            L.RooftopRoutes.Add(Route);TArray<FVector> Mate;for(auto Q:Route)Mate.Add(Opposite(Q));L.RooftopRoutes.Add(Mate);
        }
    }
    if(DenseCenter)
    {
        double CentralHouseArea=0;
        for(const auto& House:L.Houses)
        {
            const auto Size=House.Frame.GetScale3D();
            const FBox Footprint=FBox(-FVector(Size.X,Size.Y,0)*.5,FVector(Size.X,Size.Y,0)*.5+FVector(0,0,Size.Z)).TransformBy(FTransform(House.Frame.GetRotation(),House.Frame.GetLocation()));
            const double CentralDepth=FMath::Max(0.,FMath::Min(1000.,Footprint.Max.Y)-FMath::Max(-1000.,Footprint.Min.Y));
            CentralHouseArea+=Footprint.GetSize().X*CentralDepth/10000.;
        }
        // Reject fragmented candidates instead of turning the center back into
        // a square when winding streets consume too much buildable frontage.
        if(CentralHouseArea<80)return false;
    }
    if(StairPairs<1 && T.SizingTeamSize!=1)
    {
        FString Sizes;for(const auto& P:Houses)if((P.Max-P.Min).GetMin()>240)Sizes+=(P.Max-P.Min).ToString()+TEXT(";");
        UE_LOG(LogTemp,Warning,TEXT("ER_DISTRICT_REJECT seed=%d stairs=0 lots=%s"),Seed,*Sizes);return false;
    }
    if(V.ProceduralHouses)
    {
        // Record nearby, low-rise roof crossings including both flat rim caps.
        // Larger changes of height remain distinct buildings, as before.
        for(int32 A=0;A<L.Houses.Num();++A)for(int32 B=A+1;B<L.Houses.Num();++B)
        {
            auto Bounds=[](const FERLayoutHouse& H)
            {const auto S=H.Frame.GetScale3D();return FBox(-FVector(S.X,S.Y,0)*.5,FVector(S.X,S.Y,0)*.5+FVector(0,0,S.Z)).TransformBy(FTransform(H.Frame.GetRotation(),H.Frame.GetLocation()));};
            const auto One=Bounds(L.Houses[A]),Two=Bounds(L.Houses[B]);
            if(FMath::Abs(One.Max.Z-Two.Max.Z)>50)continue;
            for(int32 Axis=0;Axis<2;++Axis)
            {
                const auto& Left=One.GetCenter()[Axis]<Two.GetCenter()[Axis] ? One:Two;
                const auto& Right=One.GetCenter()[Axis]<Two.GetCenter()[Axis] ? Two:One;
                const int32 Along=1-Axis;const float Gap=Right.Min[Axis]-Left.Max[Axis];
                const float Lo=FMath::Max(Left.Min[Along],Right.Min[Along]),Hi=FMath::Min(Left.Max[Along],Right.Max[Along]);
                if(Gap<-.1 || Gap>180 || Hi-Lo<260)continue;
                FVector P0=Left.GetCenter(),P1=P0,P2=Right.GetCenter(),P3=P2;
                P0[Along]=P1[Along]=P2[Along]=P3[Along]=(Lo+Hi)*.5;
                P0[Axis]=Left.Max[Axis]-130;P1[Axis]=Left.Max[Axis]-50;
                P2[Axis]=Right.Min[Axis]+50;P3[Axis]=Right.Min[Axis]+130;
                P0.Z=P1.Z=Left.Max.Z+100;P2.Z=P3.Z=Right.Max.Z+100;
                bool Clear=true;
                for(const auto& Piece:Pieces)if(!Piece.VisualOnly)
                {
                    const FTransform F(FRotator(0,Piece.RotationYaw,0),Piece.Center);
                    const auto Obstacle=FBox(-Piece.Size*.5,Piece.Size*.5).ExpandBy(FVector(35,35,93));
                    auto From=P1,To=P2;From.Z=To.Z=FMath::Max(P1.Z,P2.Z)+10;
                    From=F.InverseTransformPosition(From);To=F.InverseTransformPosition(To);
                    if(FMath::LineBoxIntersection(Obstacle,From,To,To-From)){Clear=false;break;}
                }
                if(!Clear)continue;
                if(Gap>=60)
                {
                    FVector Deck=(P1+P2)*.5f;Deck.Z=FMath::Min(Left.Max.Z,Right.Max.Z)-10;
                    FVector DeckSize(180,180,20);// Join the roof edges without putting a second visible surface on either slab.
                    DeckSize[Axis]=Gap;
                    const FBox DeckBounds(Deck-DeckSize*.5f+FVector(.1f),Deck+DeckSize*.5f-FVector(.1f));
                    bool DeckClear=true;
                    for(const auto& Piece:Pieces)if(!Piece.VisualOnly)
                    {
                        const FTransform PieceFrame(FRotator(0,Piece.RotationYaw,0),Piece.Center);
                        DeckClear&=!DeckBounds.Intersect(FBox(-Piece.Size*.5f,Piece.Size*.5f).TransformBy(PieceFrame));
                    }
                    if(!DeckClear)continue;
                    auto& Link=Box(Deck,DeckSize,V.RoofSurface,true);
                    if(((A/2+B/2)%2)==0)
                    {
                        Link.Size.Z=FERCellSettings().WoodDepth;
                        Link.Center.Z=FMath::Min(Left.Max.Z,Right.Max.Z)-Link.Size.Z*.5f;
                        Link.Destructible=true;Link.Material=EERBlockMaterial::Wood;
                        Link.FixedSurface=nullptr;Link.CellSurface=V.WoodSurface;Link.Anchored=true;
                    }
                }
                L.Houses[A].RoofMouths.Add(One.GetCenter()[Axis]<Two.GetCenter()[Axis] ? P1:P2);
                L.Houses[B].RoofMouths.Add(One.GetCenter()[Axis]<Two.GetCenter()[Axis] ? P2:P1);
                L.RoofTransfers.Add({P0,P1,P2,P3});
            }
        }
    }
    // The market is an open court; elevated links belong between separate roofs.
    L.TownCrossing=PlazaCenters[0];
    L.LowerRoute={L.TownCrossing+FVector(0,-500,100),L.TownCrossing+FVector(0,500,100)};
    L.UpperRoute={L.TownCrossing+FVector(-1100,0,100),L.TownCrossing+FVector(1100,0,100)};
    // Each existing wall has exactly the requested 2/3/4 arch openings. With
    // no wall these are approach routes, free to connect across the open core.
    const float WallH=1300,Depth=240;
    auto WallSpan=[&](float Lo,float Hi,float Y)
    {
        if(Hi<=Lo)return;
        // A slit is 30cm wide, 80cm high, with masonry above and below.
        const int32 Count=FMath::FloorToInt32((Hi-Lo)/500);
        float At=Lo;
        for(int32 I=0;I<Count;++I)
        {
            const float X=Lo+(Hi-Lo)*(I+.5f)/Count;
            Box({(At+X-15)*.5f,Y,WallH*.5f},{X-15-At,Depth,WallH},V.WallSurface);
            Box({X,Y,360},{30,Depth,720},V.WallSurface);
            Box({X,Y,1050},{30,Depth,500},V.WallSurface);
            At=X+15;
        }
        if(Hi>At)Box({(At+Hi)*.5f,Y,WallH*.5f},{Hi-At,Depth,WallH},V.WallSurface);
    };
    for(int32 Wall=0;Wall<L.DistrictWallCount;++Wall)
    {
        const float Y=L.DistrictWallCount==1 ? 0:(Wall ? 1.f:-1.f)*L.DistrictWallOffset;float Edge=-Half.X;
        for(int32 I=0;I<GateX.Num();++I)
        {
            const float X=GateX[I],Gap=Widths[I],Lo=X-Gap*.5f,Hi=X+Gap*.5f;
            WallSpan(Edge,Lo,Y);
            Box({X,Y,(440+WallH)*.5f},{Gap,Depth,WallH-440},V.WallSurface);
            auto& Arch=Box({X,Y,220},{Gap+80,Depth+4,440},nullptr);Arch.FixedMesh=V.ArchMesh;Arch.VisualOnly=true;
            const FTransform Frame(FRotator::ZeroRotator,FVector(X,Y,0));
            L.TownGates.Add({Frame,Gap,Gap,0,{{X,Y-350,100},{X,Y,100},{X,Y+350,100}}});Edge=Hi;
            Pad({{X-Gap*.5f-100,Y-400},{X+Gap*.5f+100,Y+400}},0);
        }
        WallSpan(Edge,float(Half.X),Y);
        // Lateral annex roofs interrupt the long climb: ground -> roof -> crown.
        for(float X:{-float(Half.X)+400,float(Half.X)-400})
        for(float Sign:{-1.f,1.f})
        {
            const float RoofHeight=600;
            const FVector Annex(X,Y+Sign*(Depth*.5f+300),0);
            // Keep existing ramp mouths open through the annex arcade.
            auto AnnexWall=[&](FVector Offset,FVector Extent)
            {
                const FVector Center=Annex+Offset;
                const FBox Clearance(Center-Extent*.5f-FVector(35,35,93),Center+Extent*.5f+FVector(35,35,93));
                for(const auto* Path:{&L.UnderpassRoute,&L.UnderpassApproach})
                    for(int32 Segment=1;Segment<Path->Num();++Segment)
                        for(int32 Copy=0;Copy<2;++Copy)
                        {
                            const FVector From=Copy ? Opposite((*Path)[Segment-1]):(*Path)[Segment-1];
                            const FVector To=Copy ? Opposite((*Path)[Segment]):(*Path)[Segment];
                            if(FMath::LineBoxIntersection(Clearance,From,To,To-From))return;
                        }
                Box(Center,Extent,V.WallSurface);
            };
            AnnexWall({-290,0,290},{20,600,580});
            AnnexWall({290,0,290},{20,600,580});
            AnnexWall({0,-Sign*290,290},{560,20,580});
            for(float Side:{-1.f,1.f})AnnexWall({Side*180,Sign*290,290},{200,20,580});
            AnnexWall({0,Sign*290,410},{160,20,340});
            Box(Annex+FVector(0,0,590),{600,600,20},V.RoofSurface,true);
            Pad({{X-350,Y-1100},{X+350,Y+1100}},0);
            for(int32 Stage=0;Stage<2;++Stage)
            {
            const float Foot=Stage ? RoofHeight:0,Top=Stage ? WallH:RoofHeight;
            const float Climb=Top-Foot;
            const FTransform Ladder(FRotator(0,Sign*90,0),FVector(X+(Stage ? 0:(X>0 ? 180:-180)),Y+Sign*(Depth*.5f+(Stage ? 35:635)),Foot));
            L.Ladders.Add({Ladder,Climb+110,Climb});
            auto Metal=[&](FVector P,FVector Size)
            {
                auto& B=Box(Ladder.TransformPosition(P),Size,nullptr,false,Sign*90);
                B.Material=EERBlockMaterial::Metal;B.Destructible=true;B.CellSurface=V.IronSurface;B.Anchored=true;
            };
            for(float Rail:{-42.f,42.f})
            {
                for(float RailEdge:{-3.9f,3.9f})Metal({RailEdge,Rail,(Climb+110)*.5f},{.2f,8,Climb+110});
                for(float RailEdge:{-3.9f,3.9f})Metal({0,Rail+RailEdge,(Climb+110)*.5f},{7.6f,.2f,Climb+110});
            }
            for(float Z=25;Z<Climb;Z+=25)Metal({0,0,Z},{.2f,76,4});
            }
            L.RoomAccessRoutes.Add({Annex+FVector(0,Sign*180,RoofHeight+100),Annex+FVector(0,0,RoofHeight+100),Annex+FVector(0,-Sign*180,RoofHeight+100)});
        }
        for(float X:{-float(Half.X)+400,float(Half.X)-400})
        L.RoomAccessRoutes.Add({{X,Y-Depth*.5f,WallH+100},{X,Y,WallH+100},{X,Y+Depth*.5f,WallH+100}});

    }
    // At least one pair of small breakable cover stacks on a square's margin.
    FVector Cover;bool CoverFound=false;
    for(float Dy=-850;Dy<=850;Dy+=250)for(float Dx=-1100;Dx<=1100;Dx+=250)
    {
        if(FMath::Abs(Dy)<300)continue;
        const auto P=PlazaCenters[0]+FVector(Dx,Dy,0);const FBox Bounds(P-FVector(100,90,0),P+FVector(100,90,120));bool Clear=true;
        for(const auto& B:L.TownBuildings)Clear&=!Bounds.Intersect(B);
        for(const auto& Ramp:L.UnderpassRamps)Clear&=!(P.X>Ramp.Bounds.Min.X-100 && P.X<Ramp.Bounds.Max.X+100 && P.Y>Ramp.Bounds.Min.Y-100 && P.Y<Ramp.Bounds.Max.Y+100);
        for(const auto& Hole:L.UnderpassBreaches)Clear&=!(P.X>Hole.Min.X-100 && P.X<Hole.Max.X+100 && P.Y>Hole.Min.Y-100 && P.Y<Hole.Max.Y+100);
        for(int32 Wall=0;Wall<L.DistrictWallCount;++Wall)
        {const float Y=L.DistrictWallCount==1 ? 0:(Wall ? 1.f:-1.f)*L.DistrictWallOffset;Clear&=FMath::Abs(P.Y-Y)>Depth*.5f+100;}
        for(const auto& Path:North)for(int32 J=1;J<Path.Num();++J)Clear&=!FMath::LineBoxIntersection(Bounds.ExpandBy(FVector(70,70,1000)),Path[J-1],Path[J],Path[J]-Path[J-1]);
        for(const auto& Path:L.RoomAccessRoutes)for(int32 J=1;J<Path.Num();++J)Clear&=!FMath::LineBoxIntersection(Bounds.ExpandBy(FVector(70,70,1000)),Path[J-1],Path[J],Path[J]-Path[J-1]);
        if(Clear && !CoverFound){Cover=P;CoverFound=true;}
    }
    if(!CoverFound){UE_LOG(LogTemp,Warning,TEXT("ER_DISTRICT_REJECT seed=%d cover=0"),Seed);return false;}
    for(int32 Copy=0;Copy<2;++Copy)
    {
        const auto P=Copy ? Opposite(Cover):Cover;auto& B=Box(P+FVector(0,0,60),{140,120,120},nullptr);
        B.Destructible=true;B.Material=EERBlockMaterial::Wood;B.Anchored=true;
    }
    Pad({FVector2D(Base)-SpawnSize*.5,FVector2D(Base)+SpawnSize*.5},180);
    if(T.SizingTeamSize>0)
        PairBox(Base+FVector(0,-50,330),{1000,20,300},V.WallSurface);
    if(T.SizingTeamSize>0)
    {
        // A lateral mouth used to assume sea-level ground. Compact districts
        // can bring a foundation close to it: meet the actual final terrain,
        // then keep a short flat apron at that same height across the mouth.
        for(auto& Ramp:L.UnderpassRamps)
        {
            const FVector Direction=(Ramp.Exit-Ramp.Entry).GetSafeNormal2D();
            const FVector2D Outside(Ramp.Entry-Direction*100);
            Ramp.Entry.Z=AlleyGroundHeight(L,Outside);
            const FVector Across(-Direction.Y,Direction.X,0);
            const float HalfWidth=FMath::Abs(Direction.X)>.5 ? Ramp.Bounds.GetSize().Y*.5f:Ramp.Bounds.GetSize().X*.5f;
            const FVector Extent=Direction.GetAbs()*100+Across.GetAbs()*(HalfWidth+100);
            L.TerrainPads.Add(FBox(FVector(Ramp.Entry.X-Extent.X,Ramp.Entry.Y-Extent.Y,Ramp.Entry.Z),FVector(Ramp.Entry.X+Extent.X,Ramp.Entry.Y+Extent.Y,Ramp.Entry.Z)));
        }
        for(auto& Point:L.UnderpassRoute)for(const auto& Ramp:L.UnderpassRamps)
            if(Point.X>=Ramp.Bounds.Min.X-.01 && Point.X<=Ramp.Bounds.Max.X+.01 && Point.Y>=Ramp.Bounds.Min.Y-.01 && Point.Y<=Ramp.Bounds.Max.Y+.01)
                Point.Z=AlleyRampHeight(Ramp,FVector2D(Point))+100;
    }
    auto StandingHeight=[&](FVector2D P)
    {
        double Centre=100+AlleyGroundHeight(L,P);
        // On a slope the lower hemisphere lifts the capsule above the
        // vertical floor sample. Keep the same 7.5cm route clearance as on flat ground.
        for(int32 Sample=0;Sample<8;++Sample)
        {
            const double Angle=2*PI*Sample/8;
            const FVector2D Offset=FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*24;
            Centre=FMath::Max(Centre,AlleyGroundHeight(L,P+Offset)+(92.5-34)+FMath::Sqrt(34.0*34.0-24.0*24.0)+7.5);
        }
        return Centre;
    };
    auto Elevate=[&](TArray<FVector> Path)
    {
        TArray<FVector> Out;
        for(int32 I=0;I<Path.Num();++I)
        {
            const FVector A=I ? Path[I-1]:Path[I],B=Path[I];const int32 N=I ? FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(A,B)/25)):1;
            for(int32 J=I ? 1:0;J<=N;++J){auto P=FMath::Lerp(A,B,float(J)/N);P.Z=StandingHeight(FVector2D(P));Out.Add(P);}
        }
        return Out;
    };
    for(int32 I=0;I<North.Num();++I)
    {
        TArray<FVector> Path=North[I];const auto& Other=North[North.Num()-1-I];
        for(int32 J=Other.Num()-2;J>=0;--J)Path.Add(Opposite(Other[J]));
        Path=Elevate(Path);L.DistrictRoutes.Add(Path);L.RoomAccessRoutes.Add(Path);
    }
    if(!L.UnderpassApproach.IsEmpty())
    {
        const auto Approach=Elevate(L.UnderpassApproach);TArray<FVector> Route=Approach;Route.Pop();Route.Append(L.UnderpassRoute);
        for(int32 I=Approach.Num()-2;I>=0;--I)Route.Add(Opposite(Approach[I]));L.UnderpassRoute=MoveTemp(Route);
    }
    if(!L.UnderpassRoute.IsEmpty())L.RoomAccessRoutes.Add(L.UnderpassRoute);
    const auto Grade=Elevate(North[0]);int32 From=0,To=Grade.Num()-1;float Best=MAX_flt;
    for(int32 I=0;I<Grade.Num();++I)
    {
        float Run=0;
        for(int32 J=I+1;J<Grade.Num();++J)
        {
            Run+=FVector::Dist2D(Grade[J-1],Grade[J]);if(Run>=Best)break;
            if(Grade[I].Z-Grade[J].Z>=115){From=I;To=J;Best=Run;break;}
        }
    }
    TArray<FVector> Slope;Slope.Append(Grade.GetData()+From,To-From+1);L.GroundRoutes.Reset();L.GroundRoutes.Add(Slope);
    TArray<FVector> MateSlope;for(auto P:Slope)MateSlope.Add(Opposite(P));L.GroundRoutes.Add(MateSlope);
    for(auto& Path:L.RoomAccessRoutes)if(Path.Num()==2)Path=Elevate(Path);
    auto BotPath=[](const TArray<FVector>& Dense)
    {
        TArray<FVector> Sparse;float Run=0;
        for(int32 I=0;I<Dense.Num();++I)
        {
            if(I)Run+=FVector::Dist2D(Dense[I-1],Dense[I]);
            const bool Turn=I>0 && I+1<Dense.Num() && FVector::DotProduct((Dense[I]-Dense[I-1]).GetSafeNormal2D(),(Dense[I+1]-Dense[I]).GetSafeNormal2D())<.99;
            if(I==0 || I==Dense.Num()-1 || Turn || Run>=350){Sparse.Add(Dense[I]);Run=0;}
        }
        return Sparse;
    };
    Patrol=BotPath(L.DistrictRoutes[0]);const auto Return=BotPath(L.DistrictRoutes.Last());for(int32 I=Return.Num()-2;I>0;--I)Patrol.Add(Return[I]);
    for(int32 I=0;I<10;++I)
    {
        FVector P=Base+FVector((I%5-2)*140,(I/5 ? 1:-1)*(T.SizingTeamSize==1 ? 60:90)-(T.SizingTeamSize==1 ? 180:T.SizingTeamSize>0 ? 200:0),100);P.Z+=AlleyGroundHeight(L,FVector2D(P));
        L.AlphaSpawns.Add(FTransform(FRotator(0,90,0),P));L.OmegaSpawns.Add(FTransform(FRotator(0,270,0),Opposite(P)));
    }
    if(T.SizingTeamSize>0)
    {
        // The bank sits behind the screen; the existing road junction stays
        // in front. A reserved lateral exit keeps access independent of its door.
        TArray<FVector> Exit{L.AlphaSpawns[0].GetLocation(),Base+FVector(-650,-200,100),Base+FVector(-650,100,100),Base+FVector(0,100,100),Base+FVector(0,0,100)};
        Exit=Elevate(Exit);L.SpawnExitRoutes.Add(Exit);L.RoomAccessRoutes.Add(Exit);
        for(auto& Point:Exit)Point=Opposite(Point);L.SpawnExitRoutes.Add(Exit);L.RoomAccessRoutes.Add(Exit);
    }
    if(V.ProceduralHouses)FinishAlleyHouseModules(T,Pieces,L);
    if(T.SizingTeamSize>0)
    {
        // A resized district must not place a steep foundation transition on
        // a standing route. Check the terrain across the capsule footprint,
        // then let the bounded seeded composition retry choose another plan.
        for(const auto& Path:L.DistrictRoutes)for(int32 I=1;I<Path.Num();++I)
        {
            const int32 Samples=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(Path[I-1],Path[I])/35));
            for(int32 J=0;J<=Samples;++J)
            {
                const FVector RoutePoint=FMath::Lerp(Path[I-1],Path[I],float(J)/Samples);
                const FVector2D RouteCenter(RoutePoint);
                for(float DX:{-34.f,0.f,34.f})for(float DY:{-34.f,0.f,34.f})
                {
                    const FVector2D P=RouteCenter+FVector2D(DX,DY);
                    // Resolve the local75cm terrain triangle rather than
                    // averaging a steep edge together with its flat neighbours.
                    const float GX=(AlleyGroundHeight(L,P+FVector2D(1,0))-AlleyGroundHeight(L,P-FVector2D(1,0)))*.5f;
                    const float GY=(AlleyGroundHeight(L,P+FVector2D(0,1))-AlleyGroundHeight(L,P-FVector2D(0,1)))*.5f;
                    if(FMath::RoundToInt((GX*GX+GY*GY)*1000)>810)return false;
                }
            }
        }
    }
    if(!BuildAlleyLowCover(T,Seed,Pieces,L))return false;
    if(Pieces.Num()>=1800 || L.Houses.Num()<=(T.SizingTeamSize>0 && T.SizingTeamSize<=2 ? 3:12)){UE_LOG(LogTemp,Warning,TEXT("ER_DISTRICT_REJECT seed=%d pieces=%d houses=%d"),Seed,Pieces.Num(),L.Houses.Num());return false;}
    return true;
}
}
