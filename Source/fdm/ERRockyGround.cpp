#include "ERProceduralMap.h"
#include "ERLayoutSolids.h"
#include "ERMapPlayability.h"

namespace
{
using M=EERBlockMaterial;

// Clip the small number of rock footprints that meet the circular rim.
TArray<FVector> ClipToRim(TArray<FVector> Polygon,double Radius)
{
    constexpr int32 Sides=64;
    const double Limit=Radius*FMath::Cos(PI/Sides);
    for(int32 Side=0;Side<Sides && !Polygon.IsEmpty();++Side)
    {
        const double Angle=(Side+.5)*2*PI/Sides;
        const FVector Normal(FMath::Cos(Angle),FMath::Sin(Angle),0);
        TArray<FVector> Clipped;
        FVector From=Polygon.Last();
        double Before=FVector::DotProduct(From,Normal)-Limit;
        for(const FVector& To:Polygon)
        {
            const double After=FVector::DotProduct(To,Normal)-Limit;
            if((Before>0)!=(After>0))Clipped.Add(FMath::Lerp(From,To,Before/(Before-After)));
            if(After<=0)Clipped.Add(To);
            From=To;Before=After;
        }
        Polygon=MoveTemp(Clipped);
    }
    return Polygon;
}
}

bool ERProcedural::BuildRocky(const FERProceduralConfig& C,const FERBlockRoomConfig& B,FERProceduralLayout& Out,const FERCellSettings& Cells)
{
    const auto* Recipe=C.TemplateAsset.Get();
    if(!Recipe || Recipe->TemplateId.IsEmpty() || Recipe->Revision<1 || C.Floors!=1 ||
        B.BlockSize!=FVector(50,20,25) || !FMath::IsFinite(B.ChunkSize) || B.ChunkSize<100 ||
        !Recipe->Pieces.IsEmpty() || Recipe->Town.Enabled || Recipe->Variation.Enabled || Recipe->ShellWallMesh || Recipe->RoofMesh)return false;
    const bool Mirror=Recipe->Symmetry==EERTemplateSymmetry::Mirror;
    if(!Mirror && Recipe->Symmetry!=EERTemplateSymmetry::HalfTurn)return false;
    const double Diameter=FMath::GridSnap(Recipe->SizingTeamSize>0 ? Recipe->SizingArena.X:Recipe->ArenaSize.X,200.);
    if(!FMath::IsFinite(Diameter) || Diameter<4800 || Diameter>30000)return false;

    FERProceduralLayout L;
    L.LayoutSymmetry=Recipe->Symmetry;L.GradedTerrain=true;L.CircularArena=true;L.RockyCoverOnly=true;
    L.ArenaSize={Diameter,Diameter,1500};
    const double Radius=Diameter*.5;
    L.Corridors.Add({{-Radius,-Radius},{Radius,Radius}});
    FRandomStream Random(C.Seed);int32 NextId=1;
    auto Mate=[&](FVector P){return Mirror ? FVector(P.X,-P.Y,P.Z):FVector(-P.X,-P.Y,P.Z);};
    auto MateYaw=[&](double Yaw){return Mirror ? -Yaw:Yaw+180;};
    auto Both=[&](TArray<TArray<FVector>>& Set,TArray<FVector> Path)
    {
        Set.Add(Path);
        for(auto& Point:Path)Point=Mate(Point);
        Set.Add(MoveTemp(Path));
    };

    const double Court=Random.RandRange(100,145),Crest=Court+Random.RandRange(960,1040);
    const double TrailStart=FMath::Min(3150.,Radius-220);
    const double TunnelX=FMath::GridSnap(FMath::Max(1750.,Radius*Random.FRandRange(.67f,.69f)),50.);
    const double TunnelBend=Random.RandRange(360,395);
    const double TunnelTurn=TunnelX+FMath::Min(115.,Radius*.034);
    const double Switchback=Random.RandRange(410,550)*(Random.RandRange(0,1)==0 ? -1. : 1.);
    const double HutY=-Radius+990;
    const double HutX=FMath::Sqrt(FMath::Square(Radius-170)-FMath::Square(HutY-185))-235;
    const FVector2D Outpost(HutX,HutY);
    const double SpawnY=-Radius+330,ScreenY=-Radius+755,JoinY=-Radius+1240;
    const FVector2D BaseShelf(-1110,ScreenY+100);
    // Keep the topology independent of the detail stream so decoration does not
    // decide whether the two banks join into a high saddle.
    const uint32 RidgeVariant=HashCombineFast(uint32(C.Seed),0x79d94b13u)%3u;
    const int32 LandformSeed=Recipe->SizingTeamSize>0 ? Recipe->CompositionSeed:C.Seed;
    const uint32 Landform=HashCombine(uint32(LandformSeed),0x1527a0dfu)%7u;
    const bool OffsetHills=Radius>=3750. && Landform==3;
    const bool TerracedBasin=Radius>=3750. && Landform==4;
    const bool DiagonalRidge=Radius>=3750. && !Mirror && Landform==2;
    const bool CentralMassif=Radius>=3750. && Landform==5;
    const bool BrokenPlateaus=Radius>=3750. && Landform==1;
    const bool SeparateLandform=OffsetHills || TerracedBasin || DiagonalRidge || CentralMassif || BrokenPlateaus;
    const bool HasGully=Radius>=3750. && !SeparateLandform && RidgeVariant!=1;
    const bool HighSaddle=Radius>=3750. && !SeparateLandform && !HasGully;
    const double SummitSide=Switchback>0 ? 1.:-1.;
    const FVector2D Summit=DiagonalRidge ? FVector2D(SummitSide*1150.,-790.):
        CentralMassif ? FVector2D(SummitSide*480.,-850.):
        BrokenPlateaus ? FVector2D(SummitSide*540.,-650.):
        OffsetHills ? FVector2D(SummitSide*880.,-1040.):
        TerracedBasin ? FVector2D(SummitSide*1370.,-980.):
        FVector2D(HasGully ? SummitSide*925.:0.,HasGully ? -387.:0.);
    const FVector2D OuterPlateau(SummitSide*1520.,-1580.);
    const FVector2D OppositePlateau(Mate(FVector(OuterPlateau,0)));
    const FVector2D SummitShelf=Summit+FVector2D(HasGully ? SummitSide*25.:-SummitSide*140.,-121);
    const double TrailEntryY=FMath::Max(-TrailStart,JoinY),Length=-TrailEntryY;
    const FVector TrailEntry(HasGully || SeparateLandform ? SummitSide*705.:0.,TrailEntryY,0);
    // The lower trail moves around the rock ribs, then traverses below the
    // lookout. Both the terrain and the traversal probe use these bends.
    TArray<FVector> Trail{TrailEntry};
    Trail.Emplace(HasGully ? Summit.X+Switchback*.67:Switchback*.85,-Length*.86,0);
    Trail.Emplace(HasGully ? Summit.X+Switchback*.78:Switchback*1.22,-Length*.72,0);
    Trail.Emplace(HasGully ? Summit.X+Switchback*.28:Switchback*.64,-Length*.61,0);
    Trail.Emplace(HasGully ? Summit.X-Switchback*.33:-Switchback,-Length*.50,0);
    Trail.Emplace(HasGully ? Summit.X+Switchback*.28:-Switchback*.77,-Length*.38,0);
    if(!HasGully)Trail.Emplace(Switchback*.42,-Length*.20,0);
    Trail.Emplace(Summit,0);
    if(SeparateLandform)
    {
        Trail={TrailEntry,
            {Summit.X+SummitSide*390.,FMath::Lerp(TrailEntryY,Summit.Y,.31),0},
            {Summit.X+SummitSide*270.,FMath::Lerp(TrailEntryY,Summit.Y,.55),0},
            {Summit.X-SummitSide*245.,FMath::Lerp(TrailEntryY,Summit.Y,.76),0},
            FVector(Summit,0)};
    }
    if(BrokenPlateaus)
    {
        Trail[1]=FVector(OuterPlateau+FVector2D(-SummitSide*265.,-640.),0);
        Trail[2]=FVector(OuterPlateau,0);
        Trail[3]=FVector(FMath::Lerp(OuterPlateau,Summit,.56)+FVector2D(-SummitSide*90.,75.),0);
    }
    TArray<double> TrailRuns{0};
    for(int32 I=1;I<Trail.Num();++I)TrailRuns.Add(TrailRuns.Last()+FVector::Dist2D(Trail[I-1],Trail[I]));
    constexpr double TunnelMouthY=-RockyTunnelMouth,TunnelBottom=445;
    auto Blend=[](double T)
    {
        T=FMath::Clamp(T,0.,1.);
        return T*T*(3-2*T);
    };
    // Each tongue leaves its tunnel shoulder and hooks past the mouth of the
    // opposite bank. The court-facing switchback rises around the outside.
    const double FoldWidth=TunnelX-330.;
    auto RidgeAxis=[&](double X)
    {
        const double Phase=FMath::Abs(X)/FoldWidth;
        const double Turn=Blend((1.0-Phase)/.58);
        return SummitSide*(X<0 ? 1.:-1.)*Turn*(HasGully ? 575.:230.);
    };
    auto Terrace=[](double Height,double Pitch,double Weight)
    {
        const double Layer=FMath::FloorToDouble(Height/Pitch);
        const double T=Height/Pitch-Layer;
        return FMath::Lerp(Height,Pitch*(Layer+T*T*(3-2*T)),Weight);
    };
    const double GullyEnd=FMath::Min(1320.,Radius-1500.);
    const double GullyBend=SummitSide*(RidgeVariant==2 ? 410.:325.);
    auto GullyAxis=[&](double Y)
    {
        const double Phase=Mirror ? -FMath::Abs(Y):Y;
        const double T=Phase/GullyEnd;
        return GullyBend*(.82*FMath::Sin(PI*T)+(RidgeVariant==2 ? .18:-.18)*FMath::Sin(2*PI*T));
    };
    auto GullyWidth=[&](double Y){return FMath::Lerp(142.,205.,Blend((FMath::Abs(Y)-300.)/(GullyEnd-300.)));};
    auto GullyBed=[&](double Distance){return Court+455*(1-Blend((Distance-440.)/(GullyEnd-430.)));};
    auto Portal=[&](double Side){return FVector2D(Side*(TunnelX-TunnelBend),TunnelMouthY);};
    auto Approach=[&](double Side)
    {
        const FVector2D Mouth=Portal(Side);
        return Mouth+(Mouth-FVector2D(Side*TunnelTurn,0)).GetSafeNormal()*390;
    };
    const double TunnelCutWidth=FMath::Min(520.,TunnelX-1320.);
    struct FHill { FVector2D Center;double Radius,Height; };
    TArray<FHill> Hills;
    for(double Side:{-1.,1.})
    {
        Hills.Add({{Side*Radius*.45,-Radius*Random.FRandRange(.36f,.47f)},FMath::Max(620.,Radius*.25),double(Random.RandRange(65,115))});
        Hills.Add({{Side*Radius*.68,-Radius*Random.FRandRange(.16f,.23f)},FMath::Max(540.,Radius*.22),double(Random.RandRange(65,105))});
    }
    const FVector2D OppositeSummit(Mate(FVector(Summit,0)));
    const double LookoutHeight=DiagonalRidge ? 630.:CentralMassif ? 635.:BrokenPlateaus ? 480.:
        OffsetHills ? 565.:TerracedBasin ? 320.:Crest-Court;
    const FVector2D CaveMouth(-1210.,-125.),CaveTurn(-290.,0.);
    const FVector2D OtherCaveMouth(1210.,Mirror ? -125.:125.),OtherCaveTurn(290.,0.);
    auto CaveDistance=[&](FVector2D P)
    {
        return FMath::Sqrt(FMath::Min3(
            FMath::PointDistToSegmentSquared(FVector(P,0),FVector(CaveMouth,0),FVector(CaveTurn,0)),
            FMath::PointDistToSegmentSquared(FVector(P,0),FVector(CaveTurn,0),FVector(OtherCaveTurn,0)),
            FMath::PointDistToSegmentSquared(FVector(P,0),FVector(OtherCaveTurn,0),FVector(OtherCaveMouth,0))));
    };
    auto LandformHeight=[&](FVector2D P)
    {
        if(DiagonalRidge)
        {
            const double Along=FMath::Abs(P.X);
            const double Across=FMath::Abs(P.Y+.69*SummitSide*P.X);
            const double LowPass=1-Blend(FMath::Abs(Along-2020.)/470.);
            const double CrestHeight=650.-485.*LowPass+35.*FMath::Cos(Along/410.);
            return Terrace(CrestHeight*(1-Blend((Across-160.)/695.)),125,.53);
        }
        if(CentralMassif)
        {
            const double Distance=FVector2D(P.X*.74+SummitSide*P.Y*.17,P.Y).Size();
            return Terrace(720.*(1-Blend((Distance-455.)/1000.)),140,.54);
        }
        if(BrokenPlateaus)
        {
            auto Mesa=[&](FVector2D Centre,double Height,double Width)
            {
                const FVector2D Delta=P-Centre;
                const double Distance=FVector2D(Delta.X*.96,Delta.Y*1.12).Size();
                return Height*(1-Blend((Distance-265.)/Width));
            };
            const double Central=FMath::Max(Mesa(Summit,485.,465.),Mesa(OppositeSummit,485.,465.));
            const double Outer=FMath::Max(Mesa(OuterPlateau,375.,430.),Mesa(OppositePlateau,375.,430.));
            auto Link=[&](FVector2D From,FVector2D To)
            {
                const double Distance=FMath::PointDistToSegment(FVector(P,0),FVector(From,0),FVector(To,0));
                return 345.*(1-Blend((Distance-85.)/260.));
            };
            const double Links=FMath::Max(Link(Summit,OuterPlateau),Link(OppositeSummit,OppositePlateau));
            return Terrace(FMath::Max3(Central,Outer,Links),110,.60);
        }
        const auto Hill=[&](FVector2D Centre)
        {
            const FVector2D Delta=P-Centre;
            const double Along=Delta.X+SummitSide*Delta.Y*.30;
            const double Distance=FVector2D(Along*1.12,Delta.Y*.88).Size();
            return (LookoutHeight-55.)*(1-Blend((Distance-205.)/(OffsetHills ? 940.:680.)));
        };
        const double Shoulders=FMath::Max(Hill(Summit),Hill(OppositeSummit));
        const double RadiusFromBasin=FVector2D(P.X*.81,P.Y*1.09).Size();
        const double Depression=TerracedBasin
            ? 250*(1-Blend((RadiusFromBasin-180.)/1010.))+
              120*(1-Blend((RadiusFromBasin-480.)/370.))+
              105*(1-Blend((RadiusFromBasin-965.)/370.))+
              125*(1-Blend((RadiusFromBasin-1290.)/430.))
            : 0.;
        return Terrace(Shoulders,145,.64)-Depression;
    };
    auto Profile=[&](FVector2D P)
    {
        const double Distance=FMath::Abs(P.Y);
        double Hillside=0;
        for(const auto& Hill:Hills)
            Hillside=FMath::Max(Hillside,Hill.Height*(1-Blend(FVector2D::Distance(P,Hill.Center)/Hill.Radius)));
        const double Across=FMath::Abs(P.X);
        const double AxisY=Mirror ? -FMath::Abs(RidgeAxis(P.X)):RidgeAxis(P.X);
        const double SpineRun=(HasGully ? 1110:1370)+125*FMath::Cos(Across/490.)+65*FMath::Cos(Across/240.);
        const double SpineHeight=(Crest-Court)*(.93+.06*FMath::Cos(Across/465.)+.04*FMath::Cos(Across/225.));
        const double CentralShelf=(Crest-Court+(HighSaddle ? 60.:0.))*(1-Blend((Across-155.)/910.))*(1-Blend((Distance-115.)/1480.));
        const double Spine=SpineHeight*(1-Blend((FMath::Abs(P.Y-AxisY)-180.)/SpineRun));
        const FVector2D BaseOffset=P-BaseShelf;
        const double BaseGrade=258*(1-Blend(FVector2D(BaseOffset.X*1.08+BaseOffset.Y*.24,BaseOffset.Y*.87).Size()/490.));
        const double Shelf=Terrace(BaseGrade,82,.62);
        const double Bed=SeparateLandform ? LandformHeight(P):Terrace(HasGully ? Spine:FMath::Max(Spine,CentralShelf),220,.55);
        const double Bedding=34*FMath::Sin((P.X+.58*P.Y)/156.)*FMath::Sin((P.X-.31*P.Y)/263.)*Blend(Bed/390.);
        const double CrestFloor=Court+Bed+Hillside+Shelf+Bedding;
        const double Bank=Blend((P.Size()-Radius+880)/880.);
        const double Angle=FMath::Atan2(P.Y,P.X);
        const double SideBank=Blend((.8-Distance/Radius)/.25);
        const double Rim=SideBank*(165+36*FMath::Cos(6*Angle)+23*FMath::Sin(10*Angle));
        const double Side=P.X<0 ? -1.:1.;
        const FVector2D Mid(Side*TunnelTurn,0);
        const FVector2D TunnelPoint(FMath::ClosestPointOnSegment(FVector(P,0),FVector(Mid,0),FVector(Approach(Side),0)));
        const double Cut=Blend((FVector2D::Distance(P,TunnelPoint)-270)/TunnelCutWidth);
        const double OuterShoulder=Blend((Across-TunnelX-240)/510.)*(1-Blend(Distance/580.));
        const double Pad=Blend((FVector2D::Distance(P,Outpost)-335)/480.);
        const double Surface=Court+(CrestFloor-Court+Bank*Rim)*FMath::Min(FMath::Max(Cut,OuterShoulder),Pad);
        if(!HasGully)return Surface;
        // Below the waterline the bed forms a shallow basin. The pooled surface
        // intersects the rock along its banks while both ends stay dry.
        const double Basin=87*(1-Blend(FMath::Square(P.X/179.)+FMath::Square(P.Y/250.)));
        const double Gully=Blend((FMath::Abs(P.X-GullyAxis(P.Y))-GullyWidth(P.Y))/335.);
        const double Open=1-Blend((Distance-GullyEnd)/330.);
        return FMath::Lerp(Surface,FMath::Lerp(GullyBed(Distance)-Basin,Surface,Gully),Open);
    };
    L.TerrainCell=100;L.TerrainOrigin={-Radius-200,-Radius-200};
    const int32 Grid=FMath::RoundToInt32(Diameter/L.TerrainCell)+5;
    L.TerrainColumns=Grid;L.TerrainHeights.Reserve(Grid*Grid);
    for(int32 Y=0;Y<Grid;++Y)for(int32 X=0;X<Grid;++X)
    {
        FVector2D P=L.TerrainOrigin+FVector2D(X,Y)*L.TerrainCell;
        if(P.Y>0){P.Y=-P.Y;if(!Mirror)P.X=-P.X;}
        L.TerrainHeights.Add(int16(FMath::RoundToInt32(Profile(P))));
    }
    // Lay the climb against the outside of each tongue. Using distance along
    // the trail keeps the grade gentle through bends even where the face is steep.
    for(int32 Y=0;Y<Grid;++Y)for(int32 X=0;X<Grid;++X)
    {
        FVector2D P=L.TerrainOrigin+FVector2D(X,Y)*L.TerrainCell;
        if(P.Y>0){P.Y=-P.Y;if(!Mirror)P.X=-P.X;}
        double Distance=900.,Run=0.;
        for(int32 I=1;I<Trail.Num();++I)
        {
            const FVector2D From(Trail[I-1]),Delta=FVector2D(Trail[I])-From;
            const double T=FMath::Clamp(FVector2D::DotProduct(P-From,Delta)/Delta.SizeSquared(),0.,1.);
            const double Off=FVector2D::Distance(P,From+T*Delta);
            if(Off>=Distance)continue;
            Distance=Off;
            Run=FMath::Lerp(TrailRuns[I-1],TrailRuns[I],T);
        }
        if(FVector2D::DistSquared(P,SummitShelf)<FMath::Square(Distance))
        {
            Distance=FVector2D::Distance(P,SummitShelf);
            Run=TrailRuns.Last();
        }
        const double Finish=Run/TrailRuns.Last();
        const double Bank=Blend((P.Y-ScreenY-330.)/410.);
        const double OutpostClearance=Blend((FVector2D::Distance(P,Outpost)-335.)/480.);
        if(TerracedBasin && FVector2D(P.X*.81,P.Y*1.09).Size()<1070.)continue;
        const double Rise=LookoutHeight*FMath::Lerp(Finish,Blend(Finish),.5);
        const double Width=HasGully ? 455.:480.;
        const double LedgeEdge=220.+36*FMath::Sin(Run/137.);
        const double TrailBed=Terrace(Rise,155,.51)*(1-Blend((Distance-LedgeEdge)/Width));
        const double CrownDistance=FMath::Min(FVector2D::Distance(P,Summit),FVector2D::Distance(P,OppositeSummit));
        const double Crown=FMath::Max(0.,LookoutHeight-.60*FMath::Max(0.,CrownDistance-100.));
        const double Bed=FMath::Max(TrailBed,Crown);
        const FVector2D TunnelMid(FMath::Sign(P.X)*TunnelTurn,0);
        const FVector2D TunnelPoint(FMath::ClosestPointOnSegment(FVector(P,0),FVector(TunnelMid,0),FVector(Approach(FMath::Sign(P.X)),0)));
        const double PortalDistance=FVector2D::Distance(P,TunnelPoint);
        const double SideClearance=(Crest-Court)*Blend((PortalDistance-245.)/355.);
        const double GorgeClearance=HasGully ? FMath::Max(0.,FMath::Abs(P.X-GullyAxis(P.Y))-GullyWidth(P.Y))*.67+GullyBed(FMath::Abs(P.Y))-Court:Bed;
        int16& Height=L.TerrainHeights[Y*Grid+X];
        Height=FMath::Max(Height,int16(FMath::RoundToInt32(Court+FMath::Min3(Bed,SideClearance,GorgeClearance)*FMath::Min(Bank,OutpostClearance))));
    }
    if(CentralMassif)
    {
        for(int32 Y=0;Y<Grid;++Y)for(int32 X=0;X<Grid;++X)
        {
            const FVector2D P=L.TerrainOrigin+FVector2D(X,Y)*L.TerrainCell;
            const FVector2D Paired(Mate(FVector(P,0)));
            const double Distance=FMath::Min(CaveDistance(P),CaveDistance(Paired));
            const double Cut=1-Blend((Distance-165.)/390.);
            int16& Height=L.TerrainHeights[Y*Grid+X];
            Height=int16(FMath::RoundToInt32(FMath::Lerp(double(Height),FMath::Min(double(Height),Court+30.),Cut)));
        }
    }
    // Ease steep edges of the cuts by lowering the adjacent shoulders.
    // Two sweeps propagate each low point along the eight grid directions.
    const FIntPoint Neighbours[]={{-1,0},{-1,-1},{0,-1},{1,-1}};
    for(int32 Direction:{1,-1})for(int32 Row=0;Row<Grid;++Row)for(int32 Column=0;Column<Grid;++Column)
    {
        const int32 X=Direction>0 ? Column:Grid-1-Column;
        const int32 Y=Direction>0 ? Row:Grid-1-Row;
        int16& Height=L.TerrainHeights[Y*Grid+X];
        for(const FIntPoint& Offset:Neighbours)
        {
            const int32 OX=X+Offset.X*Direction,OY=Y+Offset.Y*Direction;
            if(OX<0 || OX>=Grid || OY<0 || OY>=Grid)continue;
            const int32 Rise=Offset.X!=0 && Offset.Y!=0 ? 96:68;
            Height=int16(FMath::Min(int32(Height),L.TerrainHeights[OY*Grid+OX]+Rise));
        }
    }
    for(int32 Pass=0;Pass<3;++Pass)
        for(int32 Y=0;Y+1<Grid;++Y)for(int32 X=0;X+1<Grid;++X)
        {
            int16& Southwest=L.TerrainHeights[Y*Grid+X];int16& Southeast=L.TerrainHeights[Y*Grid+X+1];
            int16& Northwest=L.TerrainHeights[(Y+1)*Grid+X];int16& Northeast=L.TerrainHeights[(Y+1)*Grid+X+1];
            // A cap on each triangle gradient removes small cliff faces at
            // the switchbacks without flattening the whole bed.
            auto Limit=[](int16& Pivot,int16& H,int16& V)
            {
                const FVector2D Gradient(H-Pivot,V-Pivot);
                if(Gradient.SizeSquared()<=FMath::Square(85.))return;
                const double Grade=85./Gradient.Size();
                const double Base=FMath::Min3(double(Pivot),double(H),double(V));
                Pivot=int16(FMath::RoundToInt32(Base+(Pivot-Base)*Grade));
                H=int16(FMath::RoundToInt32(Base+(H-Base)*Grade));
                V=int16(FMath::RoundToInt32(Base+(V-Base)*Grade));
            };
            if(Mirror && L.TerrainOrigin.Y+Y*L.TerrainCell>=0){Limit(Southeast,Southwest,Northeast);Limit(Northwest,Southwest,Northeast);}
            else {Limit(Southwest,Southeast,Northwest);Limit(Northeast,Southeast,Northwest);}
        }
    for(int32 Y=0;Y<Grid;++Y)for(int32 X=0;X<Grid;++X)
    {
        int16& Height=L.TerrainHeights[Y*Grid+X];
        int16& Paired=L.TerrainHeights[(Grid-1-Y)*Grid+(Mirror ? X:Grid-1-X)];
        Height=Paired=FMath::Min(Height,Paired);
    }
    // Within the central formation the walkable surface belongs to a separate
    // rock mesh. The buried terrain supports only the lower part of the climb.
    L.TerrainRockBase=L.TerrainHeights;
    for(int32 Y=0;Y<Grid;++Y)for(int32 X=0;X<Grid;++X)
    {
        FVector2D P=L.TerrainOrigin+FVector2D(X,Y)*L.TerrainCell;
        if(P.Y>0){P.Y=-P.Y;if(!Mirror)P.X=-P.X;}
        const double Formation=(1-Blend((FMath::Abs(P.X)-(SeparateLandform ? 1780.:HasGully ? 1500.:920.))/480.))*
            (1-Blend((FMath::Abs(P.Y)-1250.)/580.));
        const double Gorge=HasGully ? Blend((FMath::Abs(P.X-GullyAxis(P.Y))-GullyWidth(P.Y))/240.):1.;
        int16& Base=L.TerrainRockBase[Y*Grid+X];
        Base-=int16(FMath::RoundToInt32(FMath::Max(0.,Base-Court-45.)*.77*Formation*Gorge));
    }
    auto Ground=[&](FVector2D P){return double(TerrainFieldHeight(L,P));};
    auto Path=[&](TArray<FVector> Knots){DrapeRoute(L,Knots);return Knots;};

    auto AddRock=[&](FVector2D Start,FVector2D End,double Width,double Bottom,double Top,double Skin,bool Closed,bool Arch=false)
    {
        const FVector2D Along=(End-Start).GetSafeNormal(),Across(-Along.Y,Along.X);
        const double Yaw=FMath::RadiansToDegrees(FMath::Atan2(Along.Y,Along.X));
        FERLayoutCrag Rock;
        Rock.Center=FVector((Start+End)*.5,(Top+Bottom)*.5);
        Rock.Half={FVector2D::Distance(Start,End)*.5,Width,(Top-Bottom)*.5};
        Rock.Yaw=float(Yaw);Rock.Skin=float(Skin);Rock.Shape=Random.GetUnsignedInt();Rock.Arch=Arch;
        L.Crags.Add(Rock);
        Rock.Center=Mate(Rock.Center);Rock.Yaw=float(MateYaw(Yaw));Rock.Reflected=Mirror;
        L.Crags.Add(Rock);
        if(Closed)
        {
            auto Footprint=ClipToRim({FVector(Start-Across*Width,0),FVector(End-Across*Width,0),
                FVector(End+Across*Width,0),FVector(Start+Across*Width,0)},Radius);
            if(Footprint.Num()>=3)
            {
                L.InaccessibleGroundPolygons.Add(Footprint);
                for(auto& Point:Footprint)Point=Mate(Point);
                L.InaccessibleGroundPolygons.Add(MoveTemp(Footprint));
            }
        }
    };
    auto Place=[&](const TArray<FERKitPart>& Parts,FVector At,double Yaw)
    {
        AddKitParts(L,Parts,FTransform(FRotator(0,Yaw,0),At),false,NextId,Cells);
        AddKitParts(L,Parts,FTransform(FRotator(0,MateYaw(Yaw),0),Mate(At)),Mirror,NextId,Cells);
    };

    constexpr int32 RimSides=48;
    for(int32 Side=0;Side<RimSides/2;++Side)
    {
        const double A=-PI+2*PI*Side/RimSides,E=A+2*PI/RimSides;
        const double Angle=(A+E)*.5;
        const double SideBank=Blend((.8-FMath::Abs(FMath::Sin(Angle)))/.25);
        const double RimRadius=Radius+FMath::Lerp(265.,190+92*FMath::Cos(6*Angle)+53*FMath::Sin(10*Angle),SideBank);
        const FVector2D Start(FMath::Cos(A)*RimRadius,FMath::Sin(A)*RimRadius);
        const FVector2D End(FMath::Cos(E)*RimRadius,FMath::Sin(E)*RimRadius);
        const FVector2D Overlap=(End-Start).GetSafeNormal()*235;
        const double Width=Random.RandRange(235,260)+SideBank*65;
        const double Top=Court+Random.RandRange(1460,1570)+115*FMath::Cos(6*Angle)+72*FMath::Sin(10*Angle);
        const double Skin=Random.RandRange(70,100);
        AddRock(Start-Overlap,End+Overlap,Width,-150,Top,Skin,false);
    }

    // Each covered leg follows the diagonal from the mouth to the inside bend.
    for(double Side:{-1.,1.})
    {
        const FVector2D Mid(Side*TunnelTurn,0),Mouth=Portal(Side);
        const FVector2D In=(Mid-Mouth).GetSafeNormal(),Across(-In.Y,In.X);
        const FVector2D Center=FMath::Lerp(Mid,Mouth,.47);
        for(double Flank:{-1.,1.})
        {
            const FVector2D Wall=Center+Across*(Flank*470);
            AddRock(Wall-In*315,Wall+In*75,165,-130,Crest+Random.RandRange(-110,95),74,true);
        }
        AddRock(Center-Across*500,Center+Across*500,335,TunnelBottom,Crest+Random.RandRange(-40,65),82,false,true);
        // A common crown caps the bend and overlaps the two diagonal tunnel roofs.
        const FVector2D Crown(Side*(TunnelTurn+15),-50);
        AddRock(Crown-FVector2D(820,55),Crown+FVector2D(820,50),280,565,Crest+18,94,false);
        const FVector2D Nose(Side*(TunnelX-505),-38);
        AddRock(Nose-FVector2D(0,77),Nose+FVector2D(0,45),135,-110,Crest-95,57,true);
        // The shoulder joins the central bedrock to the inner wall of the tunnel.
        // It runs along the divide, clear of both diagonal tunnel legs below.
        const double ShoulderEnd=TunnelX-490;
        const double ShoulderStart=FMath::Max(490.,FMath::Min(950.,ShoulderEnd-950));
        const double ShoulderMiddle=FMath::Lerp(ShoulderStart,ShoulderEnd,.45);
        const double ShoulderY=Mirror ? 0.:RidgeAxis(Side*ShoulderStart);
        if(!SeparateLandform)
        {
            AddRock({Side*(ShoulderMiddle-60),-50},{Side*ShoulderEnd,-39},206,-145,Crest-105,68,true);
            const double InnerLand=FMath::Max(ShoulderStart+95,HasGully ? 1220.:0.);
            AddRock({Side*InnerLand,FMath::Clamp(ShoulderY,-185.,185.)},{Side*(ShoulderMiddle+125),-20},134,
                -145,Crest-181,57,true);
        }

        const double Outer=FMath::Min(Radius-245.,TunnelTurn+370);
        const int32 Outcrops=FMath::Max(1,FMath::RoundToInt32((Radius-Outer)/580));
        for(int32 Index=0;Index<Outcrops;++Index)
        {
            const double Left=FMath::Lerp(Outer,Radius,double(Index)/Outcrops);
            const double Right=FMath::Lerp(Outer,Radius,double(Index+1)/Outcrops);
            const double FromY=-Random.RandRange(55,180),ToY=-Random.RandRange(55,180);
            const double Width=Random.RandRange(250,295),Top=Crest-Random.RandRange(125,265),Skin=Random.RandRange(68,96);
            const double Inner=FMath::Max(Left-155,TunnelTurn+250.);
            AddRock({Side*Inner,FromY},{Side*(Right+155),ToY},Width,-155,Top,Skin,true);
        }
    }
    L.FeatureNames.Add(TEXT("RockRidge"));L.FeatureNames.Add(TEXT("RockTunnel"));L.FeatureNames.Add(TEXT("RockTrail"));

    for(int32 Index=0;Index<10;++Index)
    {
        const double Column=Index%5-2;
        const FVector2D XY(Column*126+(Index/5)*28,SpawnY+(Index/5)*128+Column*Column*12+Column*7);
        const FVector Start(XY,Ground(XY)+100);
        L.AlphaSpawns.Emplace(FRotator(0,90,0),Start);
        L.OmegaSpawns.Emplace(FRotator(0,-90,0),Mate(Start));
    }
    L.Spawn=L.AlphaSpawns[0].GetLocation();
    AddRock({-555,ScreenY+60},{555,ScreenY+60},95,-75,Court+560,51,true);
    AddRock({-545,ScreenY+15},{-52,ScreenY+80},103,-75,Court+465,58,true);
    AddRock({-320,ScreenY+40},{266,ScreenY+114},159,-75,Court+550,83,true);
    AddRock({50,ScreenY+114},{530,ScreenY+20},103,-75,Court+465,58,true);
    for(double Side:{-1.,1.})
    {
        Both(L.SpawnExitRoutes,Path({{0,SpawnY+45,0},{Side*690,SpawnY+54,0},
            {Side*747,ScreenY-140,0},{Side*753,ScreenY+145,0},{Side*690,JoinY,0}}));
        Both(L.RoomAccessRoutes,Path({{Side*690,JoinY,0},{Side*335,JoinY+70,0},TrailEntry}));
    }
    TArray<FVector> TunnelApproaches[2];
    for(int32 Index=0;Index<2;++Index)
    {
        const double Side=Index==0 ? -1.:1.;
        const FVector2D Mid(Side*TunnelTurn,0),Mouth=Portal(Side);
        TunnelApproaches[Index]=Path({{Side*690,JoinY,0},FVector(Approach(Side),0),FVector(Mouth,0),FVector(Mid,0)});
    }
    for(int32 Index=0;Index<2;++Index)
    {
        const auto& Other=TunnelApproaches[Mirror ? Index:1-Index];
        auto& Crossing=L.CrossingRoutes.Add_GetRef(TunnelApproaches[Index]);
        for(int32 Point=Other.Num()-2;Point>=0;--Point)Crossing.Add(Mate(Other[Point]));
    }
    Both(L.RooftopRoutes,Path(MoveTemp(Trail)));
    Both(L.RockScrambles,Path({FVector(Summit,0),FVector(FMath::Lerp(Summit,SummitShelf,.5),0),FVector(SummitShelf,0)}));
    Both(L.RockScrambles,Path({{-950,ScreenY-325,0},{-1080,ScreenY-230,0},{-1270,ScreenY-108,0},
        {-1275,ScreenY+73,0},FVector(BaseShelf,0)}));
    // Outcrops behind the lookout shoulders provide standing cover above the trail.
    const double CoverSide=HasGully ? SummitSide:-SummitSide;
    const FVector2D CoverStart=SeparateLandform ? Summit+FVector2D(SummitSide*305.,30.):FVector2D(HasGully ? Summit.X+CoverSide*285:CoverSide*405,HasGully ? -645.:-35.);
    const FVector2D CoverEnd=SeparateLandform ? Summit+FVector2D(SummitSide*431.,190.):FVector2D(HasGully ? Summit.X+CoverSide*550:CoverSide*670,HasGully ? -476.:-75.);
    const double CoverFoot=Ground((CoverStart+CoverEnd)*.5);
    const double CoverTop=SeparateLandform ? Ground(Summit)+143.:FMath::Clamp(FMath::Max(CoverFoot+169,Ground(SummitShelf)+135),Crest-25.,Court+1120.);
    AddRock(CoverStart,CoverEnd,85,CoverFoot-185,CoverTop,48,true);
    L.FeatureNames.Add(TEXT("RockShelves"));
    if(DiagonalRidge)L.FeatureNames.Add(TEXT("RockDiagonalRidge"));
    if(CentralMassif)L.FeatureNames.Add(TEXT("RockCentralMassif"));
    if(BrokenPlateaus)L.FeatureNames.Add(TEXT("RockBrokenPlateaus"));
    if(OffsetHills)L.FeatureNames.Add(TEXT("RockOffsetHills"));
    if(TerracedBasin)L.FeatureNames.Add(TEXT("RockTerracedBasin"));
    if(HighSaddle)L.FeatureNames.Add(TEXT("RockHighSaddle"));
    L.Routes={L.SpawnExitRoutes[0],L.CrossingRoutes[0],L.SpawnExitRoutes[1]};
    L.GroundRoutes=L.CrossingRoutes;
    if(CentralMassif)
    {
        // Two overlapping rock slabs roof the hollow. The floor is cut into
        // the height grid, with the paired sides open to the lower approaches.
        AddRock({-900.,-75.},{125.,-55.},520.,Court+300.,Court+765.,61.,false);
        auto CaveRoute=Path({FVector(CaveMouth,0),FVector(CaveTurn,0),FVector(OtherCaveTurn,0),FVector(OtherCaveMouth,0)});
        if(Mirror)Both(L.GroundRoutes,MoveTemp(CaveRoute));
        else L.GroundRoutes.Add(MoveTemp(CaveRoute));
    }
    if(HasGully)
    {
        TArray<FVector> GullyRoute;
        for(int32 Index=-6;Index<=6;++Index)
        {
            const double Y=GullyEnd*Index/6;
            GullyRoute.Emplace(GullyAxis(Y),Y,0);
        }
        L.GroundRoutes.Add(Path(MoveTemp(GullyRoute)));
        L.PoolWater.Emplace(FVector(-188,-220,Court+353),FVector(188,220,Court+414));
        L.FeatureNames.Add(TEXT("RockGully"));
    }

    // A small observation module sits beside each court. The metal deck and
    // the low parapet above it make a compact lookout over the approach.
    const FVector Base(HutX,HutY,Court);
    const double HutYaw=Random.RandRange(10,18);
    const FTransform HutFrame(FRotator(0,HutYaw,0),Base);
    const TArray<FERKitPart> Module{
        {FBox(FVector(-215,-172,0),FVector(215,-152,250)),M::Metal,0,{}},
        {FBox(FVector(-215,152,0),FVector(215,172,250)),M::Metal,0,{FBox(FVector(-70,152,0),FVector(70,172,216))}},
        {FBox(FVector(-215,-152,0),FVector(-195,152,250)),M::Metal,0,{FBox(FVector(-215,-55,127),FVector(-195,55,180))}},
        {FBox(FVector(195,-152,0),FVector(215,152,250)),M::Metal,0,{}},
        {FBox(FVector(-215,-172,250),FVector(215,172,270)),M::Metal,2,{}},
        {FBox(FVector(-195,-148,270),FVector(195,-130,370)),M::Metal,3,{}},
        {FBox(FVector(-215,-125,270),FVector(-195,95,352)),M::Metal,3,{}},
        {FBox(FVector(-188,151,270),FVector(25,171,352)),M::Metal,3,{}}
    };
    Place(Module,Base,HutYaw);
    const double LadderYaw=90+HutYaw;
    const FVector Foot=HutFrame.TransformPosition(FVector(130,185,0));
    TArray<FERKitPart> Ladder;
    for(double Side:{-39.,39.})Ladder.Add({FBox(FVector(-4,Side-4,0),FVector(4,Side+4,380)),M::Metal,2,{}});
    for(double Z=25;Z<270;Z+=25)Ladder.Add({FBox(FVector(-3,-35,Z-3),FVector(3,35,Z+3)),M::Metal,2,{}});
    Place(Ladder,Foot,LadderYaw);
    L.Ladders.Add({FTransform(FRotator(0,LadderYaw,0),Foot),275,270});
    L.Ladders.Add({FTransform(FRotator(0,MateYaw(LadderYaw),0),Mate(Foot)),275,270});
    Both(L.RoomAccessRoutes,Path({HutFrame.TransformPosition(FVector(0,335,0)),HutFrame.TransformPosition(FVector(0,70,0))}));
    L.FeatureNames.Add(TEXT("LunarOutpost"));

    // Thick beds project from the grade where there is standing clearance.
    // Overlapping courses let the same fracture run into both tunnel shoulders.
    auto AddBed=[&](FVector2D Start,FVector2D End,double Width,double Rise,double Skin)
    {
        const FVector2D Mid=(Start+End)*.5;
        const double Bottom=FMath::Min3(Ground(Start),Ground(End),Ground(Mid))-155;
        const double Top=Ground(Mid)+Rise;
        AddRock(Start,End,Width,Bottom,Top,Skin,false);
        const ERLayoutQueries::FLayoutSolids Rock(L);
        for(const auto* Routes:{&L.RooftopRoutes,&L.GroundRoutes,&L.RockScrambles})
            for(const auto& Route:*Routes)for(int32 Index=1;Index<Route.Num();++Index)
                if(!Rock.PawnClearWorld(Route[Index-1],Route[Index]))
                {
                    L.Crags.SetNum(L.Crags.Num()-2);
                    return false;
                }
        return true;
    };
    // The straight centre line meets a rock rib. Try several positions so
    // the rib stays clear of the ford and the two upper approaches.
    bool CentreBlocked=false;
    const double TrailLength=-TrailEntryY;
    const TArray<double> RibPositions=HighSaddle || SeparateLandform
        ? TArray<double>{-.76*TrailLength,-.64*TrailLength,-.56*TrailLength,-.44*TrailLength,-.32*TrailLength}
        : TArray<double>{-600.,-720.,-850.,-480.,-990.,-1100.};
    for(double Y:RibPositions)
    {
        const double HalfLength=HighSaddle ? 78.:105.;
        if(AddBed({-HalfLength,Y-25.},{HalfLength,Y+25.},83,265,37))
        {
            CentreBlocked=true;
            break;
        }
    }
    if(!CentreBlocked && Radius>=3750.)return false;

    const FVector2D InnerCover=SummitShelf+FVector2D(-SummitSide*360.,-115.);
    AddBed(InnerCover+FVector2D(-90.,-65.),InnerCover+FVector2D(100.,84.),85,175,41);
    const FVector2D OuterCover=Summit+FVector2D(SummitSide*345.,-380.);
    AddBed(OuterCover+FVector2D(-100.,-65.),OuterCover+FVector2D(130.,90.),91,165,43);

    // Join the upper trail from the flank along the exposed bedrock. These
    // short traverses offer another approach without a global jump boost.
    const ERLayoutQueries::FLayoutSolids SideSolids(L);
    const auto& Upper=L.RooftopRoutes[0];
    for(double Side:{-1.,1.})
    {
        for(double Reach:{520.,420.,325.})
        {
            const FVector Join=Upper[Upper.Num()*2/3];
            const FVector2D TraverseFoot(Join.X+Side*Reach,Join.Y-330.);
            auto Traverse=Path({FVector(TraverseFoot,0),{Join.X+Side*Reach*.54,Join.Y-165.,0},Join});
            bool Walkable=Traverse.Last().Z>Traverse[0].Z+50.;
            for(int32 Point=1;Walkable && Point<Traverse.Num();++Point)
            {
                const FVector& From=Traverse[Point-1];
                const FVector& To=Traverse[Point];
                Walkable=FMath::Abs(To.Z-From.Z)<=FVector::Dist2D(From,To)*.80+1. &&
                    SideSolids.PawnClearWorld(From,To);
            }
            if(!Walkable)continue;
            Both(L.RockScrambles,MoveTemp(Traverse));
            break;
        }
    }
    if(SeparateLandform && !CentralMassif)AddBed({-140.,-260.},{120.,-120.},168,330,60);
    const double InnerBank=HasGully ? 495.:285.;
    const double Heel=FMath::Min(TunnelX-685.,1435.);
    const double NoseY=HasGully ? -500.:-235.;
    if(!SeparateLandform)for(double Side:{-1.,1.})
    {
        AddBed({Side*InnerBank,NoseY},{Side*(InnerBank+395),NoseY+145},116,124,49);
        AddBed({Side*(InnerBank+110),NoseY+146},{Side*Heel,NoseY+342},148,201,55);
        AddBed({Side*(InnerBank+255),NoseY-174},{Side*(Heel+215),NoseY+52},108,151,43);
        AddBed({Side*465,-1240},{Side*675,-1055},130,103,41);
        AddBed({Side*540,-1060},{Side*770,-905},126,149,40);
        AddBed({Side*1380,-1030},{Side*1500,-725},149,166,46);
    }

    // Fill neither half uniformly. Short irregular strings of rock give the
    // broad fields hard cover while the marked routes keep clear approaches.
    TArray<FVector2D> Placed;
    const int32 RockPairs=FMath::Clamp(FMath::RoundToInt32(Diameter/230),13,44);
    auto Clear=[&](FVector2D XY,double Reach)
    {
        if(XY.Size()>Radius-Reach-285 || XY.Y>-580-Reach ||
            FVector2D::Distance(XY,FVector2D(Base))<Reach+440 ||
            FVector2D::Distance(XY,FVector2D(0,ScreenY))<Reach+610)return false;
        for(const auto* Routes:{&L.GroundRoutes,&L.SpawnExitRoutes,&L.RoomAccessRoutes,&L.RooftopRoutes,&L.RockScrambles})
            for(const auto& Route:*Routes)for(int32 Index=1;Index<Route.Num();++Index)
            {
                const FVector2D A(Route[Index-1]),Delta=FVector2D(Route[Index])-A;
                const double T=FMath::Clamp(FVector2D::DotProduct(XY-A,Delta)/FMath::Max(1.,Delta.SizeSquared()),0.,1.);
                if(FVector2D::DistSquared(XY,A+T*Delta)<FMath::Square(Reach+135))return false;
            }
        return !Placed.ContainsByPredicate([&](FVector2D P){return FVector2D::DistSquared(XY,P)<FMath::Square(Reach+210);});
    };
    for(int32 Attempt=0;Attempt<RockPairs*14 && Placed.Num()<RockPairs;++Attempt)
    {
        const FVector2D XY{Random.FRandRange(float(-Radius),float(Radius)),Random.FRandRange(float(-Radius),-650.f)};
        const double Long=Random.RandRange(85,235),Short=Random.RandRange(70,150),Skin=Random.RandRange(48,115);
        if(!Clear(XY,FVector2D(Long+Skin*1.6,Short+Skin*1.6).Size()))continue;
        const double Angle=Random.FRandRange(0.f,PI);
        const FVector2D Along(FMath::Cos(Angle),FMath::Sin(Angle));
        const double Z=Ground(XY),Rise=Random.RandRange(55,270);
        AddRock(XY-Along*Long,XY+Along*Long,Short,Z-165,Z+Rise,Skin,Rise>=175);
        Placed.Add(XY);
    }
    L.FeatureNames.Add(TEXT("RockOutcrops"));

    const ERLayoutQueries::FLayoutSolids Solids(L);
    for(const auto* Routes:{&L.GroundRoutes,&L.RoomAccessRoutes,&L.SpawnExitRoutes,&L.RooftopRoutes,&L.RockScrambles})
        for(const auto& Route:*Routes)for(int32 Index=1;Index<Route.Num();++Index)
        {
            if(!Solids.PawnClearWorld(Route[Index-1],Route[Index]))
            {
                UE_LOG(LogTemp,Verbose,TEXT("ER_ROCKY_REJECT seed=%d route=%s -> %s"),C.Seed,*Route[Index-1].ToString(),*Route[Index].ToString());
                for(const auto& Solid:Solids.Solids)
                {
                    if(Solid.bPawn && Solids.Crosses(Solid,Route[Index-1],Route[Index],FVector(50,50,93)))
                        UE_LOG(LogTemp,Verbose,TEXT("ER_ROCKY_ROUTE_BLOCKER at=%s half=%s"),*Solid.Frame.GetLocation().ToString(),*Solid.Half.ToString());
                }
                return false;
            }
        }
    if(!ERPlayability::InitialSpawnsOccluded(L))
    {
        UE_LOG(LogTemp,Verbose,TEXT("ER_ROCKY_REJECT seed=%d diameter=%.0f exposed spawns"),C.Seed,Diameter);
        return false;
    }
    const FTraversalAudit Audit=AuditTraversal(L);
    if(!Audit.Passed())
    {
        UE_LOG(LogTemp,Verbose,TEXT("ER_ROCKY_REJECT seed=%d reached=%d trapped=%d ladders=%d sample=%s"),
            C.Seed,Audit.Reached,Audit.Trapped,Audit.BrokenLadders,
            *(Audit.TrapSamples.IsEmpty() ? FString():Audit.TrapSamples[0].ToString()));
        return false;
    }
    Out=MoveTemp(L);return true;
}
