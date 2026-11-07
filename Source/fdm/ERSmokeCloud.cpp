#include "ERSmokeCloud.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "UObject/UnrealType.h"
#include "ERCharacter.h"
#include "ERMatch.h"
#include "ERBlockRoom.h"
#include "NiagaraDataInterfaceRenderTargetVolume.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Misc/App.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace
{
constexpr int32 NX=16, NY=16, NZ=8;
constexpr int32 SmokeCellCount=NX*NY*NZ;
int32 Index(int32 X,int32 Y,int32 Z) { return X+NX*(Y+NY*Z); }
}

AERSmokeCloud::AERSmokeCloud()
{
    bReplicates=true; bAlwaysRelevant=true; SetNetUpdateFrequency(2);
    PrimaryActorTick.bCanEverTick=true;
    Fumes=CreateDefaultSubobject<UNiagaraComponent>(TEXT("Fumes"));
    SetRootComponent(Fumes);
    Fumes->SetAutoActivate(false);
    Fumes->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Fumes->SetCastShadow(false);
    Volume=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Volume"));
    Volume->SetupAttachment(Fumes);
    Volume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Volume->SetCastShadow(false);
    Volume->bReceivesDecals=false;
    SimSize=FVector(880,880,400);
}

FLinearColor AERSmokeCloud::ColorForTeam(EERTeam Team)
{
    if(Team==EERTeam::Alpha) return FLinearColor(.08f,.26f,.4f,1.f);
    if(Team==EERTeam::Omega) return FLinearColor(.4f,.1f,.04f,1.f);
    return FLinearColor(.30f,.31f,.32f,1.f);
}

void AERSmokeCloud::OnRep_SmokeColor()
{
    if(VolumeInstance) VolumeInstance->SetVectorParameterValue(TEXT("SmokeColor"),SmokeColor);
}

void AERSmokeCloud::Prewarm(UWorld* World,TSubclassOf<AERSmokeCloud> CloudClass,const FVector& ViewPosition)
{
    if(!World || !CloudClass || !FApp::CanEverRender() || World->GetNetMode()==NM_DedicatedServer) return;
    // The fixed volume has no fluid simulation or global fog setting to warm.
    CloudClass->GetDefaultObject<AERSmokeCloud>()->Volume->PrecachePSOs();
}

double AERSmokeCloud::ServerNow() const
{
    const auto* State=GetWorld()->GetGameState<AERGameState>();
    return State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

void AERSmokeCloud::BeginPlay()
{
    Super::BeginPlay();
    if(HasAuthority())
    {
        Duration=ERServer::Get(GetWorld(),"Smoke.Duration");
        GrowthSeconds=ERServer::Get(GetWorld(),"Smoke.Growth");FadeSeconds=ERServer::Get(GetWorld(),"Smoke.Fade");DensityScale=ERServer::Get(GetWorld(),"Smoke.Density");
        const float Radius=ERServer::Get(GetWorld(),"Smoke.Radius")*100;
        SimSize=FVector(Radius*2,Radius*2,ERServer::Get(GetWorld(),"Smoke.Height")*100);
        StartedAtServerTime=ServerNow(); SetLifeSpan(Duration);
        ShapePhase=FVector(FMath::FRandRange(0.f,100.f),FMath::FRandRange(0.f,100.f),FMath::FRandRange(0.f,100.f));
    }
    if(GetNetMode()!=NM_DedicatedServer && ReleaseSound)
    {
        const float Age=FMath::Max(0.f,float(ServerNow()-StartedAtServerTime));
        if(Age<ReleaseSound->GetDuration())
            ReleaseAudio=UGameplayStatics::SpawnSoundAtLocation(this,ReleaseSound,GetActorLocation(),FRotator::ZeroRotator,
                ReleaseVolume,1.f,Age,ReleaseAttenuation,ReleaseConcurrency,true);
        UE_LOG(LogTemp,Display,TEXT("ER_SMOKE_AUDIO sound=%s started=%d spatial=%d concurrency=%d age=%.2f gain=%.8f"),
            *GetNameSafe(ReleaseSound),ReleaseAudio && ReleaseAudio->IsPlaying(),ReleaseAttenuation!=nullptr,ReleaseConcurrency!=nullptr,Age,ReleaseVolume);
    }
    SetActorEnableCollision(false);
    Fumes->Deactivate(); Fumes->SetComponentTickEnabled(false);
    SimSize=SimSize.GetAbs().BoundToBox(FVector(200,200,100),FVector(2400,2400,1000));
    VolumeOrigin=GetActorLocation();
    FHitResult Floor;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERSmokeFloor),false,this);
    if(GetWorld()->LineTraceSingleByObjectType(Floor,VolumeOrigin+FVector(0,0,10),VolumeOrigin-FVector(0,0,150),
        FCollisionObjectQueryParams(ECC_WorldStatic),Query) && Floor.ImpactNormal.Z>.5f)
        VolumeOrigin.Z=Floor.ImpactPoint.Z+1.f;
    Volume->SetStaticMesh(VolumeMesh);
    Volume->SetWorldLocation(VolumeOrigin+FVector(0,0,SimSize.Z*.5f));
    Volume->SetWorldScale3D(SimSize/100.f);
    Volume->SetVisibility(true);
    Volume->SetHiddenInGame(false);
    RebuildContainment();
    PollGeometryChanges(); // establish the local revision baseline after the initial build
    if(GetNetMode()!=NM_DedicatedServer && FApp::CanEverRender() && VolumeMaterial)
    {
        VolumeInstance=Volume->CreateDynamicMaterialInstance(0,VolumeMaterial);
        VolumeInstance->SetVectorParameterValue(TEXT("VolumeMin"),FLinearColor(VolumeOrigin-FVector(SimSize.X*.5f,SimSize.Y*.5f,0)));
        VolumeInstance->SetVectorParameterValue(TEXT("VolumeSize"),FLinearColor(SimSize));
        VolumeInstance->SetVectorParameterValue(TEXT("SmokeColor"),SmokeColor);
        UploadDensity();
    }
    else Volume->SetVisibility(false);
    UE_LOG(LogTemp,Display,TEXT("ER_SMOKE_CLOUD shape=hemisphere cells=%d grid=16x16x8 duration=%.1f replicated=%d collision=none"),FilledCellCount,Duration,GetIsReplicated());
}

void AERSmokeCloud::RebuildContainment()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(ERSmoke_Containment);
    ++ContainmentBuildCount;
    const FVector Cell=SimSize/FVector(NX,NY,NZ);
    const FVector Min=VolumeOrigin-FVector(SimSize.X*.5f,SimSize.Y*.5f,0);
    TArray<uint8> Free; Free.Init(0,SmokeCellCount); Occupancy.Init(0,SmokeCellCount);
    TArray<FVector> Centers; Centers.SetNum(SmokeCellCount);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ERSmokeContainment),false,this);
    const FCollisionObjectQueryParams Objects(ECC_WorldStatic);
    // Conservative whole-cell overlap prevents a cell spanning a thin wall.
    // Edge sweeps also catch walls falling precisely between neighbouring cells.
    const auto Shape=FCollisionShape::MakeBox(Cell*.48f);
    int32 Seed=INDEX_NONE; float Nearest=MAX_flt;
    const FVector Source=GetActorLocation()+FVector(0,0,Cell.Z*.5f);
    for(int32 Z=0;Z<NZ;++Z) for(int32 Y=0;Y<NY;++Y) for(int32 X=0;X<NX;++X)
    {
        const int32 I=Index(X,Y,Z);
        Centers[I]=Min+Cell*FVector(X+.5f,Y+.5f,Z+.5f);
        const FVector Local=(Centers[I]-VolumeOrigin)/FVector(SimSize.X*.5f,SimSize.Y*.5f,SimSize.Z);
        if(Local.SizeSquared()>1.f || GetWorld()->OverlapAnyTestByObjectType(Centers[I],FQuat::Identity,Objects,Shape,Query)) continue;
        Free[I]=1;
        const float Distance=FVector::DistSquared(Centers[I],Source);
        FHitResult Hit;
        if(Distance<Nearest && !GetWorld()->LineTraceSingleByObjectType(Hit,Source,Centers[I],Objects,Query))
        { Seed=I; Nearest=Distance; }
    }
    TArray<int32> Queue;
    // Never teleport smoke into a remote pocket when the source is enclosed.
    if(Seed!=INDEX_NONE && Nearest<FMath::Square(Cell.Size()*1.5f)) { Queue.Add(Seed); Occupancy[Seed]=255; }
    constexpr int32 DX[]={1,-1,0,0,0,0}, DY[]={0,0,1,-1,0,0}, DZ[]={0,0,0,0,1,-1};
    for(int32 Head=0;Head<Queue.Num();++Head)
    {
        const int32 I=Queue[Head], X=I%NX, Y=(I/NX)%NY, Z=I/(NX*NY);
        for(int32 D=0;D<6;++D)
        {
            const int32 XX=X+DX[D],YY=Y+DY[D],ZZ=Z+DZ[D];
            if(XX<0 || XX>=NX || YY<0 || YY>=NY || ZZ<0 || ZZ>=NZ) continue;
            const int32 J=Index(XX,YY,ZZ);
            if(!Free[J] || Occupancy[J]) continue;
            FHitResult Hit;
            if(GetWorld()->SweepSingleByObjectType(Hit,Centers[I],Centers[J],FQuat::Identity,Objects,Shape,Query)) continue;
            Occupancy[J]=255; Queue.Add(J);
        }
    }
    FilledCellCount=Queue.Num();
    if(VolumeInstance) UploadDensity();
}

bool AERSmokeCloud::PollGeometryChanges()
{
    // Only nearby collision can change the connected containment grid. Bounds
    // include the whole voxel sweep, including thin inflated collision shapes.
    const FVector Center=VolumeOrigin+FVector(0,0,SimSize.Z*.5);
    const FBox Region=(FBox(Center-SimSize*.5,Center+SimSize*.5)).ExpandBy(100);
    bool Changed=false;
    TArray<AERBlockRoom*,TInlineAllocator<4>> Present;
    for(TActorIterator<AERBlockRoom> It(GetWorld());It;++It)
    {
        auto* Room=*It;Present.Add(Room);
        auto* Watch=GeometryWatches.FindByPredicate([Room](const FGeometryWatch& W){return W.Room==Room;});
        if(!Watch){Watch=&GeometryWatches.AddDefaulted_GetRef();Watch->Room=Room;Changed=true;}
        const auto& Chunks=Room->GetCellWorld().GetChunks();
        const uint32 LayoutDigest=Room->GetCellWorld().GetLayoutDigest();
        const FTransform Transform=Room->GetActorTransform();
        if(Watch->Epoch!=Room->GetMapEpoch() || Watch->LayoutDigest!=LayoutDigest || Watch->ChunkCount!=Chunks.Num() ||
            !Watch->Transform.Equals(Transform) || Watch->Region!=Region)
        {
            Watch->Epoch=Room->GetMapEpoch();Watch->LayoutDigest=LayoutDigest;Watch->ChunkCount=Chunks.Num();Watch->Transform=Transform;Watch->Region=Region;
            Watch->Chunks.Reset();Watch->Revisions.Reset();
            const FBox Local=Region.TransformBy(Transform.Inverse());
            for(int32 I=0;I<Chunks.Num();++I)if(Chunks[I].Bounds.Intersect(Local))
            {Watch->Chunks.Add(I);Watch->Revisions.Add(Chunks[I].Revision);}
            Changed=true;
        }
        else for(int32 I=0;I<Watch->Chunks.Num();++I)
        {
            const uint32 Revision=Chunks[Watch->Chunks[I]].Revision;
            if(Watch->Revisions[I]!=Revision){Watch->Revisions[I]=Revision;Changed=true;}
        }
    }
    for(int32 I=GeometryWatches.Num()-1;I>=0;--I)
        if(!Present.Contains(GeometryWatches[I].Room.Get())){GeometryWatches.RemoveAtSwap(I);Changed=true;}
    return Changed;
}

bool AERSmokeCloud::ContainsSmokeAt(const FVector& Position) const
{
    const FVector Unit=(Position-VolumeOrigin+FVector(SimSize.X*.5f,SimSize.Y*.5f,0))/SimSize;
    if(Unit.X<0 || Unit.X>=1 || Unit.Y<0 || Unit.Y>=1 || Unit.Z<0 || Unit.Z>=1) return false;
    const int32 I=Index(int32(Unit.X*NX),int32(Unit.Y*NY),int32(Unit.Z*NZ));
    return Occupancy.IsValidIndex(I) && Occupancy[I]!=0;
}

float AERSmokeCloud::ReconDensity(const FVector& Position, float Age) const
{
    if (Age<=0 || Age>=Duration) return 0;
    const FVector Unit=(Position-VolumeOrigin+FVector(SimSize.X*.5,SimSize.Y*.5,0))/SimSize;
    if (Unit.X<0 || Unit.X>=1 || Unit.Y<0 || Unit.Y>=1 || Unit.Z<0 || Unit.Z>=1) return 0;
    // Match the material's trilinear atlas sampling, rather than treating the
    // whole enclosing box as opaque (which would seal empty/occluded pockets).
    const FVector Grid=(Unit*FVector(NX,NY,NZ)-FVector(.5)).BoundToBox(FVector::ZeroVector,FVector(NX-1,NY-1,NZ-1));
    const FIntVector Lo(FMath::FloorToInt(Grid.X),FMath::FloorToInt(Grid.Y),FMath::FloorToInt(Grid.Z));
    const FIntVector Hi(FMath::Min(Lo.X+1,NX-1),FMath::Min(Lo.Y+1,NY-1),FMath::Min(Lo.Z+1,NZ-1));
    float Occupied=0;
    for (int32 Z=0;Z<2;++Z) for (int32 Y=0;Y<2;++Y) for (int32 X=0;X<2;++X)
    {
        const int32 I=Index(X?Hi.X:Lo.X,Y?Hi.Y:Lo.Y,Z?Hi.Z:Lo.Z);
        const float Weight=(X?Grid.X-Lo.X:1-Grid.X+Lo.X)*(Y?Grid.Y-Lo.Y:1-Grid.Y+Lo.Y)*(Z?Grid.Z-Lo.Z:1-Grid.Z+Lo.Z);
        if (Occupancy.IsValidIndex(I)) Occupied+=Weight*Occupancy[I]/255.f;
    }
    if (Occupied<=0) return 0;
    const FVector Q=(Position-VolumeOrigin)/FVector(SimSize.X*.5,SimSize.Y*.5,SimSize.Z);
    const float Length=Q.Size(); const FVector Dir=Q/FMath::Max(Length,.001f);
    const float Lobes=FMath::Sin(Dir.X*4.1+ShapePhase.X+Age*.28)*FMath::Sin(Dir.Y*3.7+ShapePhase.Y-Age*.22)*FMath::Cos(Dir.Z*4.6+ShapePhase.Z+Age*.18);
    // Slow travelling billows move the silhouette without moving the cloud.
    const float Sway=FMath::Sin(Dir.X*3.1+ShapePhase.X+Age*.85)*FMath::Cos(Dir.Y*2.7+ShapePhase.Y-Age*.65);
    const float Boundary=.88f+.07f*Lobes+.04f*Sway;
    const float Patch=.5f+.5f*FMath::Sin(Dir.X*3.1+ShapePhase.X)*FMath::Cos(Dir.Y*4.3+ShapePhase.Y)*FMath::Sin(Dir.Z*2.7+ShapePhase.Z);
    const float Delay=.22f*Patch;
    const float Growth=FMath::Clamp((FMath::Clamp(Age/GrowthSeconds,0.f,1.f)-Delay)/(1-Delay),0.f,1.f);
    const float Edge=FMath::Clamp((Growth*Boundary-Length)/.2f,0.f,1.f);
    // Mean of the shader's two visual noise factors. Geometry, contour phase,
    // growth, expiry and cuts are authoritative; texture noise remains cosmetic.
    float Density=DensityScale*Occupied*Edge*.725f*.825f*FMath::Clamp((Duration-Age)/FadeSeconds,0.f,1.f);
    const double Now=GetWorld()->GetTimeSeconds();
    for (const auto& Cut:ShotCuts)
    {
        const float Radius=RecoveredRadius(Cut,Now); if (Radius<=0) continue;
        const float Distance=FMath::PointDistToSegment(Position,Cut.Start,Cut.End);
        const float Core=Radius*(Cut.Radius>=100.f ? .8f : .3f);
        Density*=FMath::SmoothStep(Core,Radius,Distance);
    }
    return Density;
}

float AERSmokeCloud::ReconOpticalDepth(const FVector& Start, const FVector& End) const
{
    const float Age=FMath::Max(0.f,float(ServerNow()-StartedAtServerTime));
    if (Age<=0 || Age>=Duration || FilledCellCount==0) return 0;
    const FVector Min=VolumeOrigin-FVector(SimSize.X*.5,SimSize.Y*.5,0), Max=Min+SimSize;
    const FVector Delta=End-Start;
    double Enter=0, Exit=1;
    for (int32 Axis=0;Axis<3;++Axis)
    {
        if (FMath::Abs(Delta[Axis])<.0001) { if (Start[Axis]<Min[Axis] || Start[Axis]>Max[Axis]) return 0; continue; }
        double A=(Min[Axis]-Start[Axis])/Delta[Axis], B=(Max[Axis]-Start[Axis])/Delta[Axis];
        if (A>B) Swap(A,B);
        Enter=FMath::Max(Enter,A); Exit=FMath::Min(Exit,B);
        if (Enter>=Exit) return 0;
    }
    const float Step=Delta.Size()*(Exit-Enter)/48.f;
    float Depth=0;
    for (int32 I=0;I<48;++I)
    {
        Depth+=ReconDensity(Start+Delta*(Enter+(I+.5)*(Exit-Enter)/48),Age)*Step*.035f;
        if (Depth>=1.f) break;
    }
    return Depth;
}

void AERSmokeCloud::UploadDensity()
{
    if(!VolumeInstance) return;
    constexpr int32 W=NX*NZ,H=NY;
    if(!DensityAtlas)
    {
        DensityAtlas=UTexture2D::CreateTransient(W,H,PF_B8G8R8A8,TEXT("ERSmokeContainment"));
        DensityAtlas->SRGB=false; DensityAtlas->Filter=TF_Bilinear;
        DensityAtlas->AddressX=TA_Clamp; DensityAtlas->AddressY=TA_Clamp;
        DensityAtlas->NeverStream=true;
        DensityAtlas->UpdateResource();
        VolumeInstance->SetTextureParameterValue(TEXT("DensityAtlas"),DensityAtlas);
    }
    auto* Pixels=new uint8[W*H*4];
    for(int32 Z=0;Z<NZ;++Z) for(int32 Y=0;Y<NY;++Y) for(int32 X=0;X<NX;++X)
    {
        const int32 P=(X+Z*NX+Y*W)*4;
        Pixels[P]=Pixels[P+1]=Pixels[P+2]=Occupancy[Index(X,Y,Z)]; Pixels[P+3]=255;
    }
    auto* Region=new FUpdateTextureRegion2D(0,0,0,0,W,H);
    DensityAtlas->UpdateTextureRegions(0,1,Region,W*4,4,Pixels,
        [](uint8* Data,const FUpdateTextureRegion2D* R){ delete[] Data; delete R; });
}

void AERSmokeCloud::AddShot(const FVector& Start,const FVector& End,float Radius,float Lifetime)
{
    if(ServerNow()-StartedAtServerTime>=Duration || (End-Start).IsNearlyZero()) return;
    const FVector Center=VolumeOrigin+FVector(0,0,SimSize.Z*.5f);
    const bool Blast=Radius>=100.f,Projectile=Radius>12.f && !Blast;
    if(Radius<=12.f){Radius=ERServer::Get(GetWorld(),"Smoke.ShotRadius")*100;Lifetime=ERServer::Get(GetWorld(),"Smoke.ShotLifetime");}
    else if(Radius>=100.f)Radius*=ERServer::Get(GetWorld(),"Smoke.BlastRadius");
    Radius=FMath::Clamp(Radius,1.f,1200.f);
    Lifetime=Blast ? FMath::Max(.1f,Duration-float(ServerNow()-StartedAtServerTime)) : FMath::Clamp(Lifetime,.1f,10.f);
    if(!FMath::LineBoxIntersection(FBox(Center-SimSize*.5f,Center+SimSize*.5f).ExpandBy(Radius),Start,End,End-Start)) return;
    if(ShotCuts.Num()>=4)
    {
        // Short projectile wakes must not immediately erase a large blast pocket.
        int32 Replace=ShotCuts.IndexOfByPredicate([](const FShotCut& Cut){return !Cut.Blast;});
        if(Replace==INDEX_NONE) { if(!Blast) return;Replace=0; }
        ShotCuts.RemoveAt(Replace);
    }
    ShotCuts.Add({Start,End,GetWorld()->GetTimeSeconds()+Lifetime,Radius,Lifetime,Blast,Projectile});
    if(ShotCuts.Num()>4) ShotCuts.RemoveAt(0,ShotCuts.Num()-4);
    ++ShotInteractionCount;
    if(Blast) { ++BlastInteractionCount;LastBlastRadius=Radius;LastBlastLifetime=Lifetime; }
    else if(Projectile) ++ProjectileInteractionCount;
}

float AERSmokeCloud::RecoveredRadius(const FShotCut& Cut,double Now) const
{
    if(Now>=Cut.Until) return 0;
    const float Remaining=FMath::Clamp(float(Cut.Until-Now)/Cut.Lifetime,0.f,1.f);
    // Blast dispersal barely refills before cloud expiry; bullet wakes close slowly.
    return Cut.Radius*(Cut.Blast ? .95f+.05f*Remaining : FMath::Sqrt(Remaining));
}
float AERSmokeCloud::GetActiveBlastRadius() const
{
    float Radius=0;for(const auto& Cut:ShotCuts) if(Cut.Blast) Radius=FMath::Max(Radius,RecoveredRadius(Cut,GetWorld()->GetTimeSeconds()));
    return Radius;
}
float AERSmokeCloud::GetActiveShotRadius() const
{
    float Radius=0;for(const auto& Cut:ShotCuts) if(!Cut.Blast && !Cut.Projectile) Radius=FMath::Max(Radius,RecoveredRadius(Cut,GetWorld()->GetTimeSeconds()));
    return Radius;
}

void AERSmokeCloud::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now>=NextGeometryCheck)
    {
        if(PollGeometryChanges())RebuildContainment();
        NextGeometryCheck=Now+.75;
    }
    ShotCuts.RemoveAll([Now](const FShotCut& Cut){ return Cut.Until<Now; });
    LastColliderCount=ShotCuts.Num();
    if(!VolumeInstance) return;
    const float Age=FMath::Max(0.f,float(ServerNow()-StartedAtServerTime));
    if(Age>=1.f && Age-DeltaSeconds<1.f)
        UE_LOG(LogTemp,Display,TEXT("ER_SMOKE_RENDER mesh=%s material=%s visible=%d location=%s scale=%s bounds=%s"),*GetNameSafe(Volume->GetStaticMesh()),*GetNameSafe(Volume->GetMaterial(0)),Volume->IsVisible(),*Volume->GetComponentLocation().ToString(),*Volume->GetComponentScale().ToString(),*Volume->Bounds.GetBox().ToString());
    VolumeInstance->SetScalarParameterValue(TEXT("Growth"),FMath::Clamp(Age/GrowthSeconds,0.f,1.f));
    VolumeInstance->SetScalarParameterValue(TEXT("Fade"),DensityScale*FMath::Clamp((Duration-Age)/FadeSeconds,0.f,1.f));
    VolumeInstance->SetScalarParameterValue(TEXT("SmokeAge"),Age);
    VolumeInstance->SetVectorParameterValue(TEXT("ShapePhase"),FLinearColor(ShapePhase));
    for(int32 I=0;I<4;++I)
    {
        const auto* Cut=ShotCuts.IsValidIndex(I) ? &ShotCuts[I] : nullptr;
        VolumeInstance->SetVectorParameterValue(*FString::Printf(TEXT("CutStart%d"),I),FLinearColor(Cut ? Cut->Start : FVector::ZeroVector));
        VolumeInstance->SetVectorParameterValue(*FString::Printf(TEXT("CutEnd%d"),I),FLinearColor(Cut ? Cut->End : FVector::ZeroVector));
        VolumeInstance->SetScalarParameterValue(*FString::Printf(TEXT("CutRadius%d"),I),Cut ? RecoveredRadius(*Cut,Now) : 0.f);
    }
}

void AERSmokeCloud::EndPlay(const EEndPlayReason::Type Reason)
{
    if(ReleaseAudio) ReleaseAudio->Stop();
    Super::EndPlay(Reason);
}
void AERSmokeCloud::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AERSmokeCloud,StartedAtServerTime);
    DOREPLIFETIME(AERSmokeCloud,GrowthSeconds);DOREPLIFETIME(AERSmokeCloud,FadeSeconds);DOREPLIFETIME(AERSmokeCloud,DensityScale);
    DOREPLIFETIME(AERSmokeCloud,Duration);DOREPLIFETIME(AERSmokeCloud,SimSize);
    DOREPLIFETIME(AERSmokeCloud,ShapePhase);
    DOREPLIFETIME(AERSmokeCloud,SmokeColor);
}

bool AERSmokeCloud::PrepareOwnedSystem(UNiagaraSystem* SmokeSystem)
{
#if WITH_EDITOR
    if(!SmokeSystem || !SmokeSystem->GetPathName().StartsWith(TEXT("/Game/EclipseRecoil/Effects/Smoke/"))) return false;
    SmokeSystem->Modify();
    auto& Store=SmokeSystem->GetExposedParameters();
    for(const auto& Variable:Store.ReadParameterVariables())
        if(Variable.GetName()==TEXT("User.outVolumeTX"))
            if(auto* Interface=Cast<UNiagaraDataInterfaceRenderTargetVolume>(Store.GetDataInterface(Variable)))
            {
                Interface->Modify();
                Interface->RenderTargetUserParameter.Parameter=FNiagaraVariable();
                Interface->bInheritUserParameterSettings=false;
                Interface->bOverrideFormat=true;
                Interface->OverrideRenderTargetFormat=RTF_RGBA16f;
                Interface->Size=FIntVector(40,40,28);
            }
    SmokeSystem->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}

FString AERSmokeCloud::DescribeSmokeSource(UBlueprint* Blueprint, UNiagaraSystem* SmokeSystem)
{
    FString Text;
#if WITH_EDITOR
    if (SmokeSystem)
    {
        const auto& Store = SmokeSystem->GetExposedParameters();
        for (const auto& Variable : Store.ReadParameterVariables())
        {
            Text += FString::Printf(TEXT("PARAM %s type=%s"),*Variable.GetName().ToString(),*Variable.GetType().GetName());
            if (const auto* Struct = Variable.GetType().GetScriptStruct())
                if (const uint8* Data=Store.GetParameterData(Variable))
                {
                    FString Value; Struct->ExportText(Value,Data,nullptr,nullptr,PPF_None,nullptr);
                    Text += TEXT(" value=")+Value;
                }
            Text += TEXT("\n");
        }
    }
    if (Blueprint && Blueprint->GeneratedClass)
    {
        UObject* Defaults=Blueprint->GeneratedClass->GetDefaultObject();
        for (TFieldIterator<FProperty> It(Blueprint->GeneratedClass,EFieldIterationFlags::None);It;++It)
        {
            FString Value; It->ExportText_InContainer(0,Value,Defaults,nullptr,Defaults,PPF_None);
            Text += FString::Printf(TEXT("PROPERTY %s = %s\n"),*It->GetName(),*Value);
        }
        TArray<UEdGraph*> Graphs=Blueprint->UbergraphPages; Graphs.Append(Blueprint->FunctionGraphs);
        for (auto* Graph : Graphs) for (const auto& Node : Graph->Nodes)
        {
            Text += FString::Printf(TEXT("NODE %s: %s\n"),*Node->GetName(),*Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            for (auto* Pin : Node->Pins)
            {
                Text += FString::Printf(TEXT("  PIN %s %s default=%s object=%s"),*Pin->PinName.ToString(),Pin->Direction==EGPD_Input ? TEXT("in") : TEXT("out"),*Pin->DefaultValue,*GetPathNameSafe(Pin->DefaultObject));
                for(auto* Link : Pin->LinkedTo) Text += FString::Printf(TEXT(" -> %s.%s"),*Link->GetOwningNode()->GetName(),*Link->PinName.ToString());
                Text += TEXT("\n");
            }
        }
    }
#endif
    return Text;
}
