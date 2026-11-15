#include "ERProceduralMap.h"
#include "ERLayoutSolids.h"
#include "Math/RotationMatrix.h"

namespace ERProcedural
{
namespace
{
constexpr double Cell=50;
constexpr double SurfaceInset=10;
constexpr double DeepBedDrop=SurfaceInset+100;
constexpr double FordBedDrop=SurfaceInset+33;
constexpr double BankGrade=.62;

double CrossingBedDrop(double DistanceFromFord)
{
    return FMath::Clamp(FordBedDrop+DistanceFromFord*BankGrade,FordBedDrop,DeepBedDrop);
}

double RoundedBox(FVector2D P,FVector2D Half,double Radius)
{
    const FVector2D Q(FMath::Abs(P.X)-Half.X+Radius,FMath::Abs(P.Y)-Half.Y+Radius);
    const FVector2D Outside(FMath::Max(Q.X,0.),FMath::Max(Q.Y,0.));
    return Radius-Outside.Size()-FMath::Min(FMath::Max(Q.X,Q.Y),0.);
}

double Ellipse(FVector2D P,FVector2D Center,FVector2D Half)
{
    return (1-((P-Center)/Half).Size())*FMath::Min(Half.X,Half.Y);
}
}

void PlanVillageGeography(FERProceduralLayout& L,int32 Seed,int32 ForcedProfile)
{
    check(ForcedProfile>=0 && ForcedProfile<=4);
    const int32 Profile=ForcedProfile ? ForcedProfile:VillageProfile(Seed);
    if(!Profile)return;
    const bool ReservedRiver=Profile==1 && L.FeatureNames.Contains(TEXT("VillageRiverCorridor"));
    const FVector2D Half=FVector2D(L.ArenaSize)*.5;
    const FVector2D Origin=-Half-FVector2D(Cell);
    const int32 NX=FMath::CeilToInt32(-2*Origin.X/Cell)+1,NY=FMath::CeilToInt32(-2*Origin.Y/Cell)+1;
    const bool Mirror=L.LayoutSymmetry==EERTemplateSymmetry::Mirror;
    auto Mate=[&](FVector2D P){return FVector2D(Mirror ? P.X:-P.X,-P.Y);};
    auto Point=[&](int32 X,int32 Y){return Origin+FVector2D(X,Y)*Cell;};
    TArray<float> Reserve;
    Reserve.Init(10000,NX*NY);
    // Leave foundations and spawn exits at their original elevations, with a
    // smooth bank outside their protected footprint. Rasterize their mates too.
    auto Protect=[&](FBox2D Box)
    {
        constexpr double Margin=420;
        const int32 X0=FMath::Max(0,FMath::FloorToInt32((Box.Min.X-Margin-Origin.X)/Cell));
        const int32 Y0=FMath::Max(0,FMath::FloorToInt32((Box.Min.Y-Margin-Origin.Y)/Cell));
        const int32 X1=FMath::Min(NX-1,FMath::CeilToInt32((Box.Max.X+Margin-Origin.X)/Cell));
        const int32 Y1=FMath::Min(NY-1,FMath::CeilToInt32((Box.Max.Y+Margin-Origin.Y)/Cell));
        for(int32 Y=Y0;Y<=Y1;++Y)for(int32 X=X0;X<=X1;++X)
        {
            const FVector2D P=Point(X,Y);
            const FVector2D D(FMath::Max3(Box.Min.X-P.X,P.X-Box.Max.X,0.),
                              FMath::Max3(Box.Min.Y-P.Y,P.Y-Box.Max.Y,0.));
            Reserve[Y*NX+X]=FMath::Min(Reserve[Y*NX+X],float(D.Size()));
        }
    };
    auto ProtectPair=[&](FBox2D Box)
    {
        Protect(Box);
        Protect(FBox2D(ForceInit)+Mate(Box.Min)+Mate(Box.Max));
    };
    auto Foundation=[&](const FBox& Box)
    {
        if(!Box.IsValid)return;
        ProtectPair(FBox2D(FVector2D(Box.Min),FVector2D(Box.Max)).ExpandBy(65));
    };
    const ERLayoutQueries::FLayoutSolids Solids(L);
    for(const auto& S:Solids.Solids)
    {
        const FVector Center=S.Bounds.GetCenter();
        if(FMath::Abs(Center.X)>=Half.X || FMath::Abs(Center.Y)>=Half.Y)continue;
        if(S.Bounds.Min.Z<VillageGroundHeight(L,FVector2D(Center))+235)Foundation(S.Bounds);
    }
    for(const auto& Box:L.TownBuildings)Foundation(Box);
    for(const auto& House:L.SecondaryStructures)
        Foundation(FBox(-House.Size*.5,House.Size*.5).TransformBy(House.Frame));
    auto ProtectPath=[&](const TArray<FVector>& Path)
    {
        for(int32 I=1;I<Path.Num();++I)
        {
            const FVector A=Path[I-1],B=Path[I];
            const int32 Steps=FMath::Max(1,FMath::CeilToInt32(FVector::Dist2D(A,B)/70));
            for(int32 J=0;J<=Steps;++J)
            {
                const FVector2D P(FMath::Lerp(A,B,double(J)/Steps));
                ProtectPair(FBox2D(P-FVector2D(100),P+FVector2D(100)));
            }
        }
    };
    for(const auto& Zone:L.SpawnZones)ProtectPath(Zone.ExitRoute);
    for(const auto& Path:L.StairRoutes)ProtectPath(Path);
    for(const auto& Site:L.CoverSites)ProtectPath({Site.Entry,Site.Hide});
    for(const auto* Starts:{&L.AlphaSpawns,&L.OmegaSpawns})
        for(const auto& Start:*Starts)
        {
            const FVector2D P(Start.GetLocation());
            ProtectPair(FBox2D(P-FVector2D(275),P+FVector2D(275)));
        }

    FRandomStream Stream(int32(HashCombine(uint32(Seed),0x47454f47u)));
    const double Bend=Stream.RandRange(90,170),FloodX=FMath::Min(1425.,Half.X*.55);
    const double FloodY=FMath::Min(1150.,Half.Y*.28),ChannelLength=Half.Y-1050;
    const FVector2D FloodCenter(FloodX,FMath::Min(Half.Y*.43,1800.));
    FVector2D Island(750,900);
    for(const auto& Block:L.VillageBlocks)
    {
        const FVector P=Block.Frame.GetLocation();
        Island.X=FMath::Max(Island.X,FMath::Abs(P.X)+Block.Size.X*.5+90);
        Island.Y=FMath::Max(Island.Y,FMath::Abs(P.Y)+Block.Size.Y*.5+220);
    }
    const FVector2D Basin(FMath::Min(Island.X+760,Half.X-620),FMath::Min(Island.Y+1000,Half.Y-1250));
    Island.X=FMath::Min(Island.X,Basin.X-540);
    Island.Y=FMath::Min(Island.Y,Basin.Y-540);
    auto Cut=[&](FVector2D P)
    {
        double Bank=0,Depth=DeepBedDrop;
        switch(Profile)
        {
        case 1:
            Bank=480-FMath::Abs(P.Y-(Mirror ? 0.:Bend*FMath::Sin(P.X/Half.X*UE_PI)));
            Depth=CrossingBedDrop(FMath::Abs(P.X)-350);
            break;
        case 2:
            Bank=FMath::Min(RoundedBox(P,Basin,560),-RoundedBox(P,Island,310));
            Depth=CrossingBedDrop(FMath::Abs(P.Y)-300);
            break;
        case 3:
        {
            Bank=FMath::Min(520-FMath::Abs(P.X-(Mirror ? Bend*FMath::Cos(P.Y/ChannelLength*UE_PI):
                Bend*FMath::Sin(P.Y/ChannelLength*UE_PI))),ChannelLength-FMath::Abs(P.Y));
            const double CenterFord=FMath::Min(320.,ChannelLength*.18);
            const double EndFord=FMath::Min(490.,ChannelLength*.22);
            Depth=CrossingBedDrop(FMath::Min(FMath::Abs(P.Y)-CenterFord,ChannelLength-EndFord-FMath::Abs(P.Y)));
            break;
        }
        default:
            Bank=FMath::Max(Ellipse(P,FloodCenter,{FloodX-370,FloodY}),
                Ellipse(P,Mate(FloodCenter),{FloodX-370,FloodY}));
            break;
        }
        return FMath::Clamp(Bank*BankGrade,0.,Depth);
    };

    TArray<int16> Heights;
    Heights.SetNumUninitialized(NX*NY);
    int32 Wet=0;
    for(int32 Y=0;Y<NY;++Y)for(int32 X=0;X<NX;++X)
    {
        const FVector2D P=Point(X,Y);
        // Keep the palisade on grade. Wet banks stop just inside the original
        // boundary instead of changing supports or opening an exit.
        const double Rim=FMath::Min(Half.X-FMath::Abs(P.X),Half.Y-FMath::Abs(P.Y));
        const double Drop=FMath::Max(0.,FMath::Min3(Cut(P),double(Reserve[Y*NX+X])*BankGrade,(Rim-50)*BankGrade));
        const double Grade=VillageGroundHeight(L,P);
        Heights[Y*NX+X]=int16(FMath::RoundToInt32(Grade-Drop));
        Wet+=Drop>SurfaceInset+1 ? 1:0;
    }
    if(Wet<100)return;
    L.PoolWater.Reset();
    L.VillageStreams.Reset();
    L.VillageRingInner.Reset();
    L.VillageRingOuter.Reset();
    L.VillageWaterwayTerrainFaces.Reset();
    L.VillageWaterwayTerrainVertices.Reset();
    L.FeatureNames.Remove(TEXT("VillageWatercourses"));
    L.FeatureNames.Remove(TEXT("VillageLoopStreams"));
    L.GradedTerrain=true;
    L.TerrainOrigin=Origin;L.TerrainCell=Cell;L.TerrainColumns=NX;
    L.TerrainHeights=MoveTemp(Heights);
    L.VillageGeography=Profile;
    L.FeatureNames.Add(TEXT("VillageGeography"));
    // Reserve submerged ground in shared construction passes. Consecutive
    // samples on a row share one reservation rather than spawning components.
    for(int32 Y=0;Y<NY;++Y)
    {
        int32 Start=INDEX_NONE;
        for(int32 X=0;X<=NX;++X)
        {
            const FVector2D P=Point(X,Y);
            const double Grade=VillageGroundHeight(L,P);
            if(X<NX && Grade-L.TerrainHeights[Y*NX+X]>SurfaceInset)
            {
                if(Start==INDEX_NONE)Start=X;
            }
            else if(Start!=INDEX_NONE)
            {
                L.PoolWater.Emplace(FVector(Point(Start,Y)-FVector2D(Cell*.5),Grade-DeepBedDrop),
                    FVector(Point(X-1,Y)+FVector2D(Cell*.5),Grade-SurfaceInset));
                Start=INDEX_NONE;
            }
        }
    }
    // Fit one deck and its mate to dry landings. Check the entire deck
    // width, since a clear center line alone can still clip a hut or a stair.
    auto FitBridge=[&](FVector2D Target,FVector2D Axis,double HalfSpan,double Width)
    {
        const FVector2D Side(Axis.Y,-Axis.X);
        // Reflected crossings run parallel on opposite flanks; their
        // full half-turn pairing is supplied by the authored river layout.
        const FVector2D Partner=ReservedRiver ? -Target:Mate(Target);
        if(FVector2D::Distance(Target,Partner)<Width+100)return false;
        const auto BridgeMate=[&](FVector2D P){return ReservedRiver ? -P:Mate(P);};
        constexpr double Ramp=150;
        const double Approach=HalfSpan+Ramp;
        TArray<FVector> Path;
        for(double Offset:{-Approach,-HalfSpan,HalfSpan,Approach})
        {
            const auto P=Target+Axis*Offset;
            const bool Deck=FMath::Abs(Offset)==HalfSpan;
            const double Height=VillageGroundHeight(L,P)+(Deck ? 22:0);
            if(FMath::Abs(P.X)>Half.X-110 || FMath::Abs(P.Y)>Half.Y-110)return false;
            if(!Deck && VillageGeographyWaterDepth(L,P)>0)return false;
            Path.Emplace(P,Height+100);
        }
        // A deck cannot sit over a reserved shelter or flank path. A
        // capsule walking that original path would hit the deck from below.
        for(bool Paired:{false,true})
        {
            const auto ToDeck=[&](FVector P)
            {
                FVector2D XY(P);
                if(Paired)XY=BridgeMate(XY);
                XY-=Target;
                return FVector(FVector2D::DotProduct(XY,Axis),FVector2D::DotProduct(XY,Side),0);
            };
            const FBox Footprint(FVector(-Approach-45,-Width*.5-45,-1),FVector(Approach+45,Width*.5+45,1));
            const auto Overlaps=[&](FVector A,FVector B)
            {
                A=ToDeck(A);B=ToDeck(B);
                return FMath::LineBoxIntersection(Footprint,A,B,B-A);
            };
            for(const auto& Site:L.CoverSites)if(Overlaps(Site.Entry,Site.Hide))return false;
            for(const auto& Zone:L.SpawnZones)
                for(int32 I=1;I<Zone.ExitRoute.Num();++I)
                    if(Overlaps(Zone.ExitRoute[I-1],Zone.ExitRoute[I]))return false;
            for(const auto* Paths:{&L.CrossingRoutes,&L.SpawnExitRoutes,&L.VillageSideBypasses,&L.VillagePassages})
                for(const auto& Route:*Paths)
                    for(int32 I=1;I<Route.Num();++I)
                        if(Overlaps(Route[I-1],Route[I]))return false;
        }
        for(bool Paired:{false,true})for(double Across:{-Width*.5,0.,Width*.5})
            for(int32 I=1;I<Path.Num();++I)
            {
                const FVector Shift(Side*Across,0);
                FVector A=Path[I-1]+Shift,B=Path[I]+Shift;
                if(Paired){A=FVector(BridgeMate(FVector2D(A)),A.Z);B=FVector(BridgeMate(FVector2D(B)),B.Z);}
                if(!Solids.PawnClearWorld(A,B) || !Solids.PawnClearWorld(A-FVector(0,0,45),B-FVector(0,0,45)))return false;
                for(double Alpha:{0.,.25,.5,.75,1.})
                {
                    const FVector P=FMath::Lerp(A,B,Alpha);
                    if(GroundHeight(L,FVector2D(P))>P.Z-96)return false;
                }
            }
        int32 WetSamples=0;
        for(double Offset=-HalfSpan;Offset<=HalfSpan;Offset+=Cell)
        {
            const FVector2D P=Target+Offset*Axis;
            WetSamples+=VillageGeographyWaterDepth(L,P)>18 ? 1:0;
        }
        if(WetSamples<5)return false;
        for(bool Paired:{false,true})
        {
            TArray<FVector> Route=Path;
            if(Paired)for(auto& P:Route)P=FVector(BridgeMate(FVector2D(P)),P.Z);
            for(int32 I=1;I<Route.Num();++I)
            {
                const FVector A=Route[I-1]-FVector(0,0,100),B=Route[I]-FVector(0,0,100);
                const FVector Delta=B-A;
                const FQuat Rotation=FRotationMatrix::MakeFromXZ(Delta,FVector::UpVector).ToQuat();
                FERLayoutFixedPiece Deck;
                Deck.Material=EERBlockMaterial::Wood;
                Deck.Transform=FTransform(Rotation,(A+B)*.5-Rotation.GetUpVector()*8,
                    FVector(Delta.Size(),Width,16)/100);
                L.FixedPieces.Add(MoveTemp(Deck));
            }
            L.VillageWaterCrossings.Add(MoveTemp(Route));
        }
        return true;
    };
    const FVector2D Target=Profile==1 ? FVector2D(-Half.X*.72,0):
        Profile==2 ? FVector2D(0,-(Island.Y+Basin.Y)*.5):Profile==3 ? FVector2D(0,-ChannelLength*.62):FVector2D(-FloodX,-FloodCenter.Y);
    const FVector2D PreferredAxis=Profile>=3 ? FVector2D(1,0):FVector2D(0,1);
    auto FindBridge=[&](FVector2D Axis,double Width)
    {
        const FVector2D Side(Axis.Y,-Axis.X);
        for(double Span:{365.,510.,655.,800.,945.,240.})
            for(double Offset:{0.,150.,-150.,300.,-300.,450.,-450.,600.,-600.,750.,-750.,900.,-900.,1050.,-1050.,1200.,-1200.})
                for(double Shift:{0.,150.,-150.,300.,-300.,450.,-450.})
                    if(FitBridge(Target+Side*Offset+Axis*Shift,Axis,Span,Width))return true;
        return false;
    };
    const double PreferredWidth=Profile==1 ? 450:Profile==4 ? 170:260;
    if(ReservedRiver)
    {
        const double X=-Half.X+480;
        const double Y=Mirror ? 0.:Bend*FMath::Sin(X/Half.X*UE_PI);
        if(!FitBridge({X,Y},PreferredAxis,490,320) && !FitBridge({X,Y},PreferredAxis,490,170))return;
        L.VillageFord={{0,-680,100},{0,680,100}};
        DrapeRoute(L,L.VillageFord,100);
        for(int32 XStep=-250;XStep<=250;XStep+=50)
        {
            if(VillageGeographyWaterDepth(L,{double(XStep),0})<25)
            {
                L.VillageFord.Reset();
                return;
            }
            auto Foot=[&](double Y){return FVector(XStep,Y,GroundHeight(L,{double(XStep),Y})+100);};
            for(int32 YStep=-650;YStep<650;YStep+=25)
                if(!Solids.PawnClearWorld(Foot(YStep),Foot(YStep+25)))
                {
                    L.VillageFord.Reset();
                    return;
                }
        }
    }
    else if(!FindBridge(PreferredAxis,PreferredWidth) && PreferredWidth>170 && !FindBridge(PreferredAxis,170))
    {
        constexpr double Diagonal=.5;
        const FVector2D Tilt(Diagonal,FMath::Sqrt(1-Diagonal*Diagonal));
        if(!FindBridge(Tilt,170))FindBridge(FVector2D(-Tilt.X,Tilt.Y),170);
    }
    // Only drape ground segments. Leave stairs, elevated landings and
    // authored spawn exits on their original physical structures.
    for(auto* Paths:{&L.Routes,&L.CrossingRoutes,&L.GroundRoutes})
        for(auto& Path:*Paths)
        {
            if(!Path.ContainsByPredicate([&](const FVector& P)
                {return FMath::Abs(P.Z-VillageGroundHeight(L,FVector2D(P))-100)>2;}))
                DrapeRoute(L,Path,100);
        }
    L.GroundRoutes.Append(L.VillageWaterCrossings);
    if(!L.VillageFord.IsEmpty())L.GroundRoutes.Add(L.VillageFord);
    for(auto& Piece:L.FixedPieces)
        if(Piece.VisualOnly && !Piece.Mesh && Piece.Transform.GetScale3D().X>1000)
            Piece.Transform.AddToTranslation({0,0,-108});
    UE_LOG(LogTemp,Display,TEXT("ER_VILLAGE_GEOGRAPHY seed=%d profile=%d wet_m2=%.1f bridges=%d"),Seed,Profile,Wet*Cell*Cell/10000,L.VillageWaterCrossings.Num());
}

double VillageGeographyWaterDepth(const FERProceduralLayout& L,FVector2D P)
{
    return VillageGroundHeight(L,P)-SurfaceInset-TerrainFieldHeight(L,P);
}
}
