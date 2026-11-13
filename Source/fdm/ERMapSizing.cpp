#include "ERProceduralMap.h"

//750m² per expected player on open ground, both teams included, scaled by AreaDensity for
//streets and sealed rooms. Dimensions are physical plan dimensions; upper floors never add
//to the area and player admission is separate.
bool ERProcedural::ConfigureTeamSize(UERMapTemplate& T,int32 TeamSize,int32 Seed)
{
    if(TeamSize<1 || TeamSize>16)return false;
    T.SizingTeamSize=TeamSize;T.CompositionSeed=Seed;
    T.SizingRoomsGroundOnly=TeamSize==1;
    FRandomStream Composition(int32(HashCombine(uint32(Seed),0x56415259u)));
    const float Area=TargetAreaM2(TeamSize,T.Archetype);
    const float Growth=FMath::Clamp((TeamSize-2)/14.f,0.f,1.f);
    auto Grid=[&](float Small,float Large){return FMath::GridSnap(FMath::Lerp(Small,Large,Growth),100.f);};
    auto Envelope=[&](float Aspect,float MinWidth,float MinLength)
    {
        FRandomStream Shape(int32(HashCombine(uint32(Seed),0x41524541u)));
        const float Ratio=Aspect*Shape.FRandRange(.96f,1.04f);
        float Width=FMath::GridSnap(FMath::Sqrt(Area*10000/Ratio),100.f);
        Width=FMath::Clamp(Width,MinWidth,Area*11000/MinLength);
        Width=FMath::GridSnap(Width,100.f);
        const float Length=FMath::Max(MinLength,FMath::GridSnap(Area*10000/Width,100.f));
        T.SizingArena={Width,Length};
    };
    switch(T.Archetype)
    {
    case EERMapArchetype::UrbanTown:
    {
        FRandomStream Shape(int32(HashCombine(uint32(Seed),0x544f574eu)));
        const double Gross=Area/.72,Ratio=Shape.FRandRange(1.12f,1.3f);
        const double Width=FMath::Max(4000.,FMath::GridSnap(FMath::Sqrt(Gross*10000/Ratio),50.));
        T.SizingArena={Width,FMath::Max(5000.,FMath::GridSnap(Gross*10000/Width,50.))};
    }
        break;
    case EERMapArchetype::CentralBuilding:
        if(T.CentralBuilding.CircularTerrain)
        {
            const float Diameter=FMath::GridSnap(FMath::Sqrt(Area*40000/PI),50.f);
            T.SizingArena={Diameter,Diameter};
        }
        else Envelope(1.f,3000,3400);
        T.CentralBuilding.ArenaMin=T.CentralBuilding.ArenaMax=T.SizingArena;
        if(T.CentralBuilding.CircularTerrain)
        {
            T.CentralBuilding.BuildingWidth={1100,1500};
            T.CentralBuilding.BuildingDepth={1900,2200};
            T.CentralBuilding.MinFloors=T.CentralBuilding.MaxFloors=TeamSize==1 ? 2 : TeamSize>=4 ? 4 : 3;
        }
        else
        {
            T.CentralBuilding.BuildingWidth={1800,Grid(1800,2400)};
            T.CentralBuilding.BuildingDepth={2200,Grid(2200,3000)};
            T.CentralBuilding.MinFloors=1;
            T.CentralBuilding.MaxFloors=TeamSize<=2 ? 1 : TeamSize<=4 ? 2 : 3;
        }
        break;
    case EERMapArchetype::MiddleEasternAlleys:
        Envelope(TeamSize==1 ? 1.f:1.25f,3200,3200);
        T.Town.ArenaMin=T.Town.ArenaMax=T.SizingArena;
        T.Town.MinAccesses=2;T.Town.MaxAccesses=TeamSize<=2 ? 2:TeamSize<=4 ? 3:4;
        T.Town.MinCentralWalls=0;T.Town.MaxCentralWalls=TeamSize<=4 ? 1:2;
        T.Town.BuildingHeight={400,Grid(500,800)};
        if(TeamSize==1)T.Town.LowCoverAndUnderpass=false;
        break;
    case EERMapArchetype::FireTraining:
        T.FireTraining.FloorArea={Area*.9f,Area*1.1f};
        T.FireTraining.MaxAspect=1.5f;
        T.FireTraining.MinCeilingHeight=800;T.FireTraining.MaxCeilingHeight=TeamSize<=2 ? 1000:1400;
        T.FireTraining.MaxPlazas=0;
        break;
    case EERMapArchetype::Rooms:
    {
        // Keep the smallest profile, but sample complete room graphs and their
        // proportions for larger budgets, rather than fixing every lane count.
        const float S[]={0,800,1200,1400,1600,1800,2000,2100,2200,1800,1900,2000,2100,2200,2200,2300,2400};
        const float Offset[]={0,400,800,800,900,1000,1100,1200,1300,1200,1300,1400,1500,1600,1700,1700,1700};
        const float Margin[]={0,300,400,500,600,700,800,900,1000,900,900,1000,1000,1100,1100,1100,1100};
        T.Rooms.MinLaneRooms=T.Rooms.MaxLaneRooms=TeamSize<=8 ? 2:3;
        T.Rooms.NominalRoomSize=S[TeamSize];T.Rooms.LaneOffset=Offset[TeamSize];T.Rooms.PlanMargin=Margin[TeamSize];
        T.Rooms.LaneSpacing=TeamSize==1 ? 800:TeamSize==2 ? 800:TeamSize==14 ? 2300:TeamSize>=15 ? 2400:S[TeamSize];
        if(TeamSize>1)
        {
            // Seed the topology within the authored dimension range. Build
            // then fits primary floor area by changing room dimensions.
            const float ProfileArea=FMath::Min(Area,16000.f);
            struct FProfile {int32 N;float S,Offset,Margin,Spacing;};
            TArray<FProfile> Profiles;
            for(int32 N=1;N<=(TeamSize==2 ? 3:4);++N)
                for(float Size=N==1 || (TeamSize==2 && N==3) ? 800.f:1200.f;Size<=2400;Size+=100)
                    for(float O=600;O<=1700;O+=100)for(float M=300;M<=800;M+=100)
                    {
                        if(N>1 && !(TeamSize==2 && N==3) && O<800)continue;
                        const float Width=3*Size+2*O+2*M;
                        const float MinimumSpacing=N>1 && Size>1600 ? (N%2==0 ? 1300.f:900.f):800.f;
                        const float Spacing=FMath::Clamp(FMath::GridSnap((ProfileArea*10000/Width-(N==1 ? 3:N)*Size-400)/(N==1 ? 2:N-1),100.f),MinimumSpacing,2400.f);
                        const FVector Bounds(Width,(N==1 ? 3:N)*Size+(N==1 ? 2:N-1)*Spacing+400,1300);
                        if(FMath::Abs(Bounds.X*Bounds.Y/10000-ProfileArea)<=ProfileArea*.1)Profiles.Add({N,Size,O,M,Spacing});
                    }
            if(Profiles.IsEmpty())return false;
            // Choose a lane count first so the many six-room proportions do
            // not crowd the denser topology out of the small-area sampler.
            TArray<int32> Counts;for(const auto& P:Profiles)Counts.AddUnique(P.N);
            const int32 N=Counts[Composition.RandRange(0,Counts.Num()-1)];
            Profiles.RemoveAll([&](const FProfile& P){return P.N!=N;});
            const auto& P=Profiles[Composition.RandRange(0,Profiles.Num()-1)];
            T.Rooms.MinLaneRooms=T.Rooms.MaxLaneRooms=P.N;
            T.Rooms.NominalRoomSize=P.S;T.Rooms.LaneOffset=P.Offset;T.Rooms.PlanMargin=P.Margin;T.Rooms.LaneSpacing=P.Spacing;
            T.SizingRoomsGroundOnly=P.N==1 || (TeamSize==2 && P.N==3);
        }
        break;
    }
    case EERMapArchetype::Village:
    {
        if(TeamSize==1)
        {
            Envelope(1.3f,2800,3600);
            T.Village.ArenaMin=T.Village.ArenaMax=T.SizingArena;
        }
        else
        {
            // The builder fits the area rectangle AFTER sampling actual block
            // footprints. This admits a short wide single block and a longer
            // pair at the same density instead of rejecting one in advance.
            T.Village.ArenaMin={3000,4400};T.Village.ArenaMax={20000,24000};
        }
        const int32 MinBlocks=TeamSize<=2 ? 1:FMath::Clamp((TeamSize+1)/2,2,5);
        const int32 MaxBlocks=TeamSize==1 ? 1:FMath::Min(5,MinBlocks+1);
        const int32 Count=Composition.RandRange(MinBlocks,MaxBlocks);
        T.Village.MinCentralBlocks=T.Village.MaxCentralBlocks=Count;
        T.Village.BlockWidth={2000,TeamSize==1 || (TeamSize==2 && Count==2) ? 2200.f:3000.f};T.Village.BlockDepth={600,TeamSize==1 ? 700.f:800.f};
        T.Village.MinStoreys=1;T.Village.MaxStoreys=TeamSize==1 ? 1:TeamSize<=4 ? 2:3;
        T.Village.StoreyBudget=TeamSize==1 ? 1:TeamSize<=4 ? 6:10;
        break;
    }
    default:break;
    }
    return T.Archetype==EERMapArchetype::UrbanTown || T.Archetype==EERMapArchetype::Rooms || T.Archetype==EERMapArchetype::Village || T.Archetype==EERMapArchetype::FireTraining || T.Archetype==EERMapArchetype::LegacyAssembly || (T.Archetype==EERMapArchetype::CentralBuilding && T.CentralBuilding.CircularTerrain ? FMath::Abs(PI*FMath::Square(T.SizingArena.X*.5)/10000-Area)<=Area*.1:ValidTeamArea(FVector(T.SizingArena,0),TeamSize,T.Archetype));
}
