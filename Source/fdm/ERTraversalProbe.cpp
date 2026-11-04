#include "ERTraversalProbe.h"
#include "ERLadder.h"
#include "ERCharacter.h"
#include "ERMovement.h"
#include "ERServerSettings.h"
#include "ERWeapon.h"
#include "ERMatch.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "InputKeyEventArgs.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
AERTraversalProbe::AERTraversalProbe()
{
    bReplicates=true;bAlwaysRelevant=true;PrimaryActorTick.bCanEverTick=true;
    Fixture=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TraversalFixture"));RootComponent=Fixture;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Fixture->SetStaticMesh(Cube.Object);Fixture->SetCollisionProfileName(TEXT("BlockAll"));Fixture->SetMobility(EComponentMobility::Movable);
}
void AERTraversalProbe::BeginPlay()
{
    Super::BeginPlay();
    auto Box=[&](FVector P,FVector Size){Fixture->AddInstance(FTransform(FQuat::Identity,P,Size/100));};
    Box({0,0,3990},{2200,2200,20});Box({-220,0,4310},{400,400,20});
    for(float Y:{-39.f,39.f})Box({0,Y,4215},{8,2,430});
    for(float Z=25;Z<320;Z+=25)Box({0,0,4000+Z},{8,76,2});
    Ladder=GetWorld()->SpawnActor<AERLadder>(FVector(0,0,4000),FRotator::ZeroRotator);
    Ladder->Height=430;Ladder->DeckHeight=320;
}
void AERTraversalProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(AERTraversalProbe,Phase);}
void AERTraversalProbe::Check(bool Value,const TCHAR* Label)
{UE_LOG(LogTemp,Display,TEXT("ER_TRAVERSAL_%s phase=%d %s"),Value?TEXT("PASS"):TEXT("FAIL"),Phase,Label);Failed|=!Value;}
void AERTraversalProbe::Place(AERCharacter* C,FVector P)
{
    C->TeleportTo(P,FRotator(0,180,0),false,true);C->GetCharacterMovement()->StopMovementImmediately();
    C->GetCharacterMovement()->SetMovementMode(MOVE_Falling);C->Health->Health=100;C->Health->Armor=100;C->Health->ProtectionEndServerTime=0;
    Cast<UERMovement>(C->GetCharacterMovement())->RetryAfter=0;C->ForceNetUpdate();
}
void AERTraversalProbe::Input(APlayerController* PC,FKey Key,EInputEvent Event)
{
    FInputKeyEventArgs Args;Args.Key=Key;Args.Event=Event;Args.AmountDepressed=Event==IE_Released?0:1;
    Args.EventTimestamp=FPlatformTime::Cycles64();PC->InputKey(Args);
}
void AERTraversalProbe::Next()
{
    ++Phase;Time=PeakSpeed=0;SawLadder=SawFall=Sent=false;EquipmentChecks=0;ForceNetUpdate();
    if(Phase<=6)
    {
        Place(Client,{55,0,4095});
        if(Phase==1 || Phase==3)
            for(int32 I=0;I<Client->Inventory.Num();++I)if(Client->Inventory[I]->Data && !Client->Inventory[I]->Data->bOneHanded && Client->Inventory[I]->Data->Payload==EERPayload::Bullet){Client->ServerSelectWeapon(I);break;}
        if(Phase==2 || Phase==4)
            for(int32 I=0;I<Client->Inventory.Num();++I)if(Client->Inventory[I]->Data && Client->Inventory[I]->Data->bOneHanded && Client->Inventory[I]->Data->Payload==EERPayload::Bullet){Client->ServerSelectWeapon(I);break;}
        if(Phase==4){Place(Client,{-100,0,4415});Client->Controller->SetControlRotation(FRotator::ZeroRotator);}
        if(Phase==5 || Phase==6){Place(Client,{55,0,4342.5});Client->GetCharacterMovement()->SetMovementMode(MOVE_Custom,2);}
    }
    if(Phase==7)Place(Host,{600,0,4195});
    if(Phase==8)Place(Host,{600,0,4695});
    if(Phase==9)Place(Host,{600,0,5295});
}
void AERTraversalProbe::Tick(float Delta)
{
    Super::Tick(Delta);
#if !UE_BUILD_SHIPPING
    if(!HasAuthority())
    {
        auto* C=Cast<AERCharacter>(UGameplayStatics::GetPlayerPawn(this,0));if(!C || !Ladder)return;
        auto* PC=Cast<APlayerController>(C->Controller);if(!PC)return;
        auto* M=Cast<UERMovement>(C->GetCharacterMovement());
        if(LocalPhase!=Phase)
        {
            LocalPhase=Phase;Time=PeakSpeed=0;SawLadder=Sent=false;EquipmentChecks=0;
            for(FKey Key:{EKeys::W,EKeys::S,EKeys::SpaceBar})Input(PC,Key,IE_Released);
            PC->SetControlRotation(FRotator(0,Phase==4?0:180,0));
            if(Phase==6){for(int32 I=Fixture->GetInstanceCount()-1;I>=4;--I)Fixture->RemoveInstance(I);}
        }
        const float Previous=Time;Time+=Delta;
        if(Phase>=1 && Phase<=3 && Previous<.3f && Time>=.3f)
        {
            Input(PC,EKeys::W,IE_Pressed);
        }
        if(Phase==1)
        {
            if(Previous<.65f && Time>=.65f)Input(PC,EKeys::W,IE_Released);
            // Reliable RPCs exercise all selections while stationary on a rung.
            const float Moments[]={.9f,1.6f,2.3f,3.f};
            for(int32 Step=0;Step<4;++Step)if(Previous<Moments[Step] && Time>=Moments[Step])
                for(int32 I=0;I<C->Inventory.Num();++I)
                {
                    const auto* D=C->Inventory[I]->Data.Get();if(!D)continue;
                    if((Step==0 && D->Payload==EERPayload::Melee) ||
                       (Step==1 && AERWeapon::IsHandGrenade(D->Payload)) ||
                       (Step==2 && D->bOneHanded && D->Payload==EERPayload::Bullet) ||
                       (Step==3 && !D->bOneHanded && D->Payload==EERPayload::LaunchedHE))
                    {C->ServerSelectWeapon(I);break;}
                }
            if(Previous<4.f && Time>=4.f){Input(PC,EKeys::SpaceBar,IE_Pressed);Input(PC,EKeys::SpaceBar,IE_Released);}
            if(Time>3.7f && !Sent)
            {
                const bool Okay=M->IsOnLadder() && C->Weapon && C->Weapon->Data->Payload==EERPayload::LaunchedHE && C->IsWeaponStowed();
                UE_LOG(LogTemp,Display,TEXT("ER_TRAVERSAL_CLIENT_%s latest_heavy_selected_and_stowed"),Okay?TEXT("PASS"):TEXT("FAIL"));Sent=true;
            }
        }
        if(Phase==4)
        {
            if(Previous<.3f && Time>=.3f)Input(PC,EKeys::W,IE_Pressed);
            if(M->IsOnLadder() && !Sent){Input(PC,EKeys::W,IE_Released);Input(PC,EKeys::S,IE_Pressed);Sent=true;}
        }
        if(Phase==5 && Previous<.5f && Time>=.5f){Input(PC,EKeys::SpaceBar,IE_Pressed);Input(PC,EKeys::SpaceBar,IE_Released);}
        if(Phase==2 || Phase==3)
        {
            if(SawLadder && !M->IsOnLadder() && C->GetActorLocation().Z>4400)Input(PC,EKeys::W,IE_Released);
            SawLadder|=M->IsOnLadder();
            if(M->IsOnLadder())
            {
                PeakSpeed=FMath::Max(PeakSpeed,FMath::Abs(M->Velocity.Z));
                if(C->IsWeaponStowed())EquipmentChecks|=1;
                if(C->Weapon && C->Weapon->GetSpreadDegrees()>=C->Weapon->Data->StandingSpreadDegrees+C->Weapon->GetBloomDegrees()+.5f)EquipmentChecks|=2;
            }
            if(Time>2 && !Sent)
            {
                const bool Okay=SawLadder && FMath::IsNearlyEqual(PeakSpeed,ERServer::Get(GetWorld(),Phase==2?FName("Move.LadderLight"):FName("Move.LadderHeavy"))/.036f,3) && (Phase!=3 || (EquipmentChecks&1)!=0) &&
                    (Phase!=2 || (EquipmentChecks&2)!=0);
                UE_LOG(LogTemp,Display,TEXT("ER_TRAVERSAL_CLIENT_%s phase=%d predicted_ladder speed=%.1f unarmed=%d"),Okay?TEXT("PASS"):TEXT("FAIL"),Phase,PeakSpeed,C->IsWeaponStowed());Sent=true;
            }
        }
        if(Phase==10 && !Sent)
        {
            bool DeadHost=false;for(TActorIterator<AERCharacter> It(GetWorld());It;++It)if(It->GetLocalRole()==ROLE_SimulatedProxy)DeadHost|=It->Health->bDead;
            if(Time>1){UE_LOG(LogTemp,Display,TEXT("ER_TRAVERSAL_CLIENT_%s replicated_fatal_fall"),DeadHost?TEXT("PASS"):TEXT("FAIL"));Sent=true;}
        }
        return;
    }
    if(!Host || !Client)
    {
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
            if(It->Get()->IsLocalController())Host=Cast<AERCharacter>(It->Get()->GetPawn());else Client=Cast<AERCharacter>(It->Get()->GetPawn());
        if(!Host || !Client || Client->Inventory.IsEmpty())return;
    }
    Time+=Delta;auto* M=Cast<UERMovement>(Client->GetCharacterMovement());
    if((Phase==2 || Phase==4) && Time>1 && M->IsOnLadder() && FMath::Abs(M->Velocity.Z)>100 && !EquipmentChecks)
    {
        EquipmentChecks=1;
        auto* W=Client->Weapon.Get();
        const float Spread=W->GetSpreadDegrees();
        Check(Spread>=W->Data->StandingSpreadDegrees+W->GetBloomDegrees()+.5f,
            Phase==2?TEXT("pistol real cone widens while ascending"):TEXT("pistol real cone widens while descending"));
        const uint32 Shots=W->AuthorizedShotCount;
        Client->ServerStartFire();Client->ServerStopFire();
        Check(W->AuthorizedShotCount==Shots+1 && FMath::IsNearlyEqual(W->LastShotSpreadDegrees,Spread,.01f),
            TEXT("moving ladder pistol shot uses the displayed cone"));
    }
    if(Phase==3 && M->IsMantling() && M->CustomMovementMode==3 && !EquipmentChecks)
    {EquipmentChecks=1;Check(Client->IsWeaponStowed(),TEXT("heavy weapon stays stowed during roof transition"));}
    if(Phase==4 && int32(Time)!=int32(Time-Delta))UE_LOG(LogTemp,Display,TEXT("ER_TRAVERSAL_DESCEND time=%.1f pos=%s mode=%d/%d accel=%s can=%d nearby=%d"),Time,*Client->GetActorLocation().ToString(),M->MovementMode.GetValue(),M->CustomMovementMode,*M->GetCurrentAcceleration().ToString(),M->CanClimbLadder(),M->FindLadder(true)!=nullptr);
    SawLadder|=M->IsOnLadder();SawFall|=M->IsFalling();
    if(M->IsOnLadder())PeakSpeed=FMath::Max(PeakSpeed,FMath::Abs(M->Velocity.Z));
    if(Phase==0 && Time>3){Place(Host,{600,500,4095});Next();}
    else if(Phase==1)
    {
        const float Moments[]={.8f,1.4f,2.1f,2.8f,3.7f};
        for(int32 Step=0;Step<5;++Step)if(Time>Moments[Step] && !(EquipmentChecks & (1<<Step)))
        {
            EquipmentChecks|=1<<Step;
            const auto* D=Client->Weapon?Client->Weapon->Data.Get():nullptr;
            bool Okay=M->IsOnLadder() && D;
            if(Step==0 || Step==4)
            {
                Okay &= Client->IsWeaponStowed();
                if(D)
                {
                    const int32 Ammo=Client->Weapon->MagazineAmmo;
                    Client->ServerStartFire();Client->ServerReload();Client->Weapon->ServerCycleScope();
                    Okay &= Client->Weapon->MagazineAmmo==Ammo && !Client->Weapon->bReloading && !Client->Weapon->ScopeLevel;
                    if(Step==4)Okay &= D->Payload==EERPayload::LaunchedHE;
                }
            }
            else Okay &= D && !Client->IsWeaponStowed() &&
                ((Step==1 && D->Payload==EERPayload::Melee) || (Step==2 && AERWeapon::IsHandGrenade(D->Payload)) ||
                 (Step==3 && D->bOneHanded && D->Payload==EERPayload::Bullet));
            Check(Okay,TEXT("ladder inventory selection and held/stowed combat state"));
            if(Step>=1 && Step<=3 && Okay)
            {
                auto* W=Client->Weapon.Get();const uint32 Shots=W->AuthorizedShotCount;
                if(Step==3)Check(FMath::IsNearlyEqual(W->GetSpreadDegrees(),D->bUseWorksheetAccuracy ?
                    ERWeaponAccuracy::Degrees(D->HipAccuracy.Spread+2*D->HipAccuracy.Ladder+W->GetFiringInaccuracy()) :
                    D->StandingSpreadDegrees+W->GetBloomDegrees(),.01f),TEXT("stationary ladder pistol uses its accuracy profile"));
                Client->ServerStartFire();
                auto* Grenade=W->CookedGrenade.Get();
                const bool Armed=Step!=2 || (Grenade && W->bCookingGrenade && Grenade->bHeld);
                Client->ServerStopFire();
                Check(W->AuthorizedShotCount==Shots+1 && Armed && (Step!=2 || !Grenade->bHeld),
                    TEXT("held knife, grenade and pistol usable on ladder"));
                // Keep this test's grenade from killing the player in subsequent movement cases.
                if(Grenade)Grenade->Destroy();
            }
        }
        if(Time>5){Check(SawLadder && !M->IsOnLadder() && !Client->IsWeaponStowed() && Client->Weapon && Client->Weapon->Data->Payload==EERPayload::LaunchedHE,TEXT("detach restores latest selection"));Next();}
    }
    else if((Phase==2 && Time>7) || (Phase==3 && Time>5.5f))
    {
        Check(SawLadder && FMath::IsNearlyEqual(PeakSpeed,ERServer::Get(GetWorld(),Phase==2?FName("Move.LadderLight"):FName("Move.LadderHeavy"))/.036f,3) && Client->GetActorLocation().Z>4400 && !Client->IsWeaponStowed(),TEXT("server validated climb, speed and roof exit"));Next();
    }
    else if(Phase==4 && Time>4){Check(SawLadder && Client->GetActorLocation().Z<4110,TEXT("descend from roof to ground"));Next();}
    else if(Phase==5 && Time>2){Check(SawFall && !M->IsOnLadder() && !Client->IsWeaponStowed(),TEXT("Space detaches and restores weapon"));Next();}
    else if(Phase==6)
    {
        if(!Sent){for(int32 I=Fixture->GetInstanceCount()-1;I>=4;--I)Fixture->RemoveInstance(I);Sent=true;}
        if(Time>2){Check(!M->IsOnLadder() && !Client->IsWeaponStowed() && Client->GetActorLocation().Z<4110,TEXT("missing rungs remove route and restore weapon"));Next();}
    }
    else if(Phase==7 && Time>2){Check(Host->Health->Health==100,TEXT("short fall safe"));Next();}
    else if(Phase==8 && Time>2){Check(Host->Health->Health>60 && Host->Health->Health<66 && Host->Health->Armor==100,TEXT("six meter fall damages health without armor absorption"));Next();}
    else if(Phase==9 && Time>2){Check(Host->Health->bDead,TEXT("fatal fall uses normal death path"));Next();}
    else if(Phase==10 && Time>2)
    {UE_LOG(LogTemp,Display,TEXT("ER_TRAVERSAL_COMPLETE %s"),Failed?TEXT("FAIL"):TEXT("PASS"));FPlatformMisc::RequestExit(false);}
#endif
}
