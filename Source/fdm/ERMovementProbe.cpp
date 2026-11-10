#include "ERMovementProbe.h"
#include "ERCharacter.h"
#include "ERMovement.h"
#include "ERWeapon.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "HAL/PlatformMisc.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
#include "UnrealClient.h"
#include "GameFramework/PlayerController.h"
AERMovementProbe::AERMovementProbe()
{
    bReplicates=true;bAlwaysRelevant=true;PrimaryActorTick.bCanEverTick=true;
    Fixture=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MovementFixture"));RootComponent=Fixture;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Fixture->SetStaticMesh(Cube.Object);Fixture->SetCollisionProfileName(TEXT("BlockAll"));Fixture->SetMobility(EComponentMobility::Movable);
}
void AERMovementProbe::BeginPlay()
{
    Super::BeginPlay();
    // Temporary collision fixture replaces the retired laboratory asset.
    for(TActorIterator<AActor> It(GetWorld());It;++It)if(*It!=this && !Cast<APawn>(*It) && !Cast<AERWeapon>(*It))It->SetActorEnableCollision(false);
    auto Box=[&](FVector P,FVector Size){Fixture->AddInstance(FTransform(FQuat::Identity,P,Size/100));};
    Box({500,0,-25},{3400,4600,50});
    for(int32 I=0;I<20;++I){const float H=(I+1)*10;Box({float(I/5)*650,float(I%5-2)*650,H*.5f},{200,200,H});}
    Box({-1200,0,125},{50,4600,250});Box({2200,0,125},{50,4600,250});
    Box({500,-2300,125},{3400,50,250});Box({500,2300,125},{3400,50,250});
    Box({-100000,0,90},{500,500,20}); // index25: shared, initially parked ceiling
}
void AERMovementProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const { Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AERMovementProbe,Phase); }
void AERMovementProbe::Check(bool Value,const FString& Label)
{
    UE_LOG(LogTemp,Display,TEXT("ER_MOVEMENT_%s %s"),Value ? TEXT("PASS") : TEXT("FAIL"),*Label);
    Failed |= !Value;
}
void AERMovementProbe::Advance() { ++Phase; Elapsed = 0; Peak = 0; SawTraversal = false; ForceNetUpdate(); }
void AERMovementProbe::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
#if !UE_BUILD_SHIPPING
    if (!HasAuthority())
    {
        auto* C = Cast<AERCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(),0));
        if (C && Phase >= 25 && Phase <= 29)
        {
            auto* M = Cast<UERMovement>(C->GetCharacterMovement());
            M->bWalkHeld = Phase == 26;
            M->bClimbHeld = Phase >= 28;
            if (LocalPhase != Phase) { LocalPhase=Phase; Elapsed=0; C->StopJumping(); if(C->Controller)C->Controller->SetControlRotation(FRotator::ZeroRotator); }
            const float Previous = Elapsed; Elapsed += DeltaTime;
            if (Phase == 29 && Previous < .6f && Elapsed >= .6f) C->Jump();
            const float TargetX = Phase == 27 ? 650.f : (Phase == 28 ? 1300.f : 1950.f);
            if (Phase < 27 || (Elapsed > (Phase==29 ? .6f:.25f) && C->GetActorLocation().X < TargetX-30)) C->AddMovementInput(FVector(1,0,0));
        }
        if (C && Phase >= 33 && Phase <= 39)
        {
            auto* M = Cast<UERMovement>(C->GetCharacterMovement());
            if (LocalPhase != Phase)
            {
                LocalPhase = Phase; Elapsed = 0; Peak = 0;
                if(C->Controller)C->Controller->SetControlRotation(FRotator::ZeroRotator);
                M->bWalkHeld = false; M->bClimbHeld = false;
                C->StopJumping();
                if(Phase==39)
                {
                    const FVector P=C->GetActorLocation();
                    Fixture->UpdateInstanceTransform(25,FTransform(FQuat::Identity,FVector(-100000,0,90),FVector(5,5,.2)),false,true,true);
                }
                // These phases isolate continuous posture intent and physical
                // clearance. Phases40..43 separately drive real C/Ctrl bindings.
                if(Phase>=37){M->bProneWanted=Phase==37;C->UnCrouch();}
            }
            const float Previous=Elapsed;Elapsed+=DeltaTime;
            if(Phase==37 && Previous<1.5f && Elapsed>=1.5f)
            {
                const FVector P=C->GetActorLocation();
                Fixture->UpdateInstanceTransform(25,FTransform(FQuat::Identity,FVector(P.X,P.Y,90),FVector(5,5,.2)),false,true,true);
            }
            auto Send=[&](float At,FKey Key,EInputEvent Event)
            {
                if(Previous>=At || Elapsed<At)return;
                FInputKeyEventArgs Args;Args.Key=Key;Args.Event=Event;Args.AmountDepressed=Event==IE_Released?0.f:1.f;
                Args.EventTimestamp=FPlatformTime::Cycles64();Cast<APlayerController>(C->Controller)->InputKey(Args);
            };
            if(Phase==33 || Phase==36)
            {Send(.05f,EKeys::W,IE_Pressed);Send(.12f,EKeys::W,IE_Released);Send(.22f,EKeys::W,IE_Pressed);}
            if(Phase==35 || Phase==37)Send(.05f,EKeys::W,IE_Released);
            if(Phase==33 && Elapsed>1.2f && Peak==0 && M->Velocity.Size2D()>490 && C->Weapon)
            {
                const FVector SprintVelocity=M->Velocity;
                const float SprintCone=C->Weapon->GetSpreadDegrees();
                M->Velocity=SprintVelocity.GetSafeNormal2D()*(1400.f/3.6f);
                const float RunCone=C->Weapon->GetSpreadDegrees();M->Velocity=SprintVelocity;
                UE_LOG(LogTemp,Display,TEXT("ER_MOVEMENT_CLIENT_%s owner_sprint_has_extra_spread run=%.3f sprint=%.3f"),
                    SprintCone>RunCone+1.4f?TEXT("PASS"):TEXT("FAIL"),RunCone,SprintCone);
                UE_LOG(LogTemp,Display,TEXT("ER_MOVEMENT_CLIENT_%s owner_stamina_drains"),
                    M->GetStaminaFraction()>.7f && M->GetStaminaFraction()<1.f ? TEXT("PASS") : TEXT("FAIL"));
                Peak=1;
            }
            if (Phase == 34 && Elapsed>20.f && Peak==0)
            {
                UE_LOG(LogTemp,Display,TEXT("ER_MOVEMENT_CLIENT_%s owner_stamina_refills"),
                    !M->IsSprinting() && M->GetStaminaFraction()==1.f ? TEXT("PASS") : TEXT("FAIL"));
                Peak=1;
            }
            if (Phase == 37 && Elapsed > 1.6f && Peak == 0)
            {
                const bool OwnerReady = FMath::IsNearlyEqual(C->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),34.f,.1f) && M->IsProne();
                bool ProxyReady = false;
                for (TActorIterator<AERCharacter> It(GetWorld()); It; ++It)
                    if (It->GetLocalRole() == ROLE_SimulatedProxy)
                        ProxyReady |= FMath::IsNearlyEqual(It->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),34.f,.1f) && Cast<UERMovement>(It->GetCharacterMovement())->IsProne();
                UE_LOG(LogTemp,Display,TEXT("ER_MOVEMENT_CLIENT_%s owner_and_simulated_proxy_prone"),OwnerReady && ProxyReady ? TEXT("PASS") : TEXT("FAIL"));
                Peak = 1;
            }

        }
        if (C && Phase >= 40 && Phase <= 43)
        {
            auto* PC = Cast<APlayerController>(C->Controller);
            if (!PC) return;
            if (LocalPhase != Phase) { LocalPhase=Phase; Elapsed=0; Peak=0; PC->SetControlRotation(FRotator::ZeroRotator); }
            const float Previous = Elapsed; Elapsed += DeltaTime;
            auto Send = [PC,Previous,this](float At, FKey Key, EInputEvent Event)
            {
                if (Previous < At && Elapsed >= At)
                {
                    FInputKeyEventArgs Args;
                    Args.Key=Key; Args.Event=Event; Args.AmountDepressed=Event==IE_Released ? 0.f : 1.f;
                    Args.EventTimestamp=FPlatformTime::Cycles64();
                    PC->InputKey(Args);
                }
            };
            const FKey Key = Phase==40 ? EKeys::W : (Phase==43 ? EKeys::LeftControl : EKeys::C);
            Send(.05f,Key,IE_Pressed); Send(.12f,Key,IE_Released);
            if (Phase != 42) { Send(.22f,Key,IE_Pressed); Send(Phase==40 ? 1.f : .3f,Key,IE_Released); }
        }
        if (C && Phase >= 48 && Phase <= 53)
        {
            auto* PC=Cast<APlayerController>(C->Controller);
            if (LocalPhase!=Phase) { LocalPhase=Phase; Elapsed=0; Peak=0; PC->SetControlRotation(FRotator::ZeroRotator); }
            const float Previous=Elapsed; Elapsed+=DeltaTime;
            auto Send=[&](float At,FKey Key,EInputEvent Event)
            {
                if (Previous>=At || Elapsed<At) return;
                FInputKeyEventArgs Args; Args.Key=Key; Args.Event=Event;
                Args.AmountDepressed=Event==IE_Released?0.f:1.f;
                Args.EventTimestamp=FPlatformTime::Cycles64(); PC->InputKey(Args);
            };
            if (Phase==48)
            {
                Send(.5f,EKeys::C,IE_Pressed); Send(.65f,EKeys::C,IE_Released);
                Send(1.8f,EKeys::W,IE_Pressed); Send(2.2f,EKeys::E,IE_Pressed);
            }
            if (Phase==49) Send(.05f,EKeys::E,IE_Released);
            if (Phase==50)
            {
                Send(.05f,EKeys::E,IE_Pressed); Send(.1f,EKeys::W,IE_Released);
                Send(.7f,EKeys::W,IE_Pressed);
            }
            if (Phase==51)
            {
                Send(.05f,EKeys::W,IE_Released); Send(.8f,EKeys::W,IE_Pressed);
            }
            if (Phase==52)
            {
                Send(.05f,EKeys::W,IE_Released); Send(.2f,EKeys::W,IE_Pressed);
                Send(.3f,EKeys::W,IE_Released); Send(.4f,EKeys::W,IE_Pressed);
                Send(.8f,EKeys::E,IE_Released);
            }
            if (Phase==53) { Send(.05f,EKeys::W,IE_Released); Send(.05f,EKeys::E,IE_Released); }
            if ((Phase==48 || Phase==50 || Phase==52) && Peak==0 && Elapsed>(Phase==48?2.7f:1.2f))
            {
                UE_LOG(LogTemp,Display,TEXT("ER_MOVEMENT_CLIENT_%s sprint_E_phase_%d"),
                    Cast<UERMovement>(C->GetCharacterMovement())->IsSprinting()?TEXT("PASS"):TEXT("FAIL"),Phase);
                Peak=1;
            }
        }
        return;
    }
    if (!Host || !Client)
    {
        for (FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            auto* Candidate=Cast<AERCharacter>(It->Get()->GetPawn());
            if(!Candidate)continue;
            auto Park=[&](float Y)
            {
                Candidate->TeleportTo(FVector(-700,Y,95),FRotator::ZeroRotator,false,true);
                Candidate->GetCharacterMovement()->StopMovementImmediately();Candidate->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            };
            if(It->Get()->IsLocalController()){if(!Host)Park(-1900);Host=Candidate;}
            else {if(!Client)Park(1900);Client=Candidate;}
        }
        if (!Host || !Client) return;
        if (FParse::Param(FCommandLine::Get(),TEXT("ERPostureProbe")))
        {
            Phase = 33; ForceNetUpdate();
            Host->TeleportTo(FVector(-900,-1900,95),FRotator::ZeroRotator);
        }
    }
    auto* Move = Cast<UERMovement>(Host->GetCharacterMovement());
    if(Phase==33 && FParse::Param(FCommandLine::Get(),TEXT("ERMovementBaseProbe")))
    {
        UE_LOG(LogTemp,Display,TEXT("ER_MOVEMENT_COMPLETE %s"),Failed?TEXT("FAIL"):TEXT("PASS"));
        FPlatformMisc::RequestExit(false);return;
    }
    if (Phase == 0)
    {
        Check(Move != nullptr && FMath::IsNearlyEqual(Host->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()*2,185),TEXT("standing capsule 185cm; custom movement loaded"));
        Host->TeleportTo(FVector(-700,-1900,95),FRotator::ZeroRotator);
        Advance(); return;
    }
    Elapsed += DeltaTime;
    if (Phase == 1 || Phase == 2)
    {
        Move->bWalkHeld = Phase == 2; Host->AddMovementInput(FVector(1,0,0));
        if (Phase == 1 && Peak == 0 && Move->Velocity.Size2D() > 0 && Move->Velocity.Size2D() < Move->MaxWalkSpeed-1) Peak = Move->Velocity.Size2D();
        if (Elapsed > 1.5f)
        {
            Check(FMath::IsNearlyEqual(Move->Velocity.Size2D(), Phase == 1 ? 1400.f/3.6f : 750.f/3.6f,1),Phase == 1 ? TEXT("14 km/h run") : TEXT("7.5 km/h Shift walk"));
            if (Phase == 1) Check(Peak > 0 && Peak < Move->MaxWalkSpeed,TEXT("progressive acceleration"));
            Advance();
        }
        return;
    }
    if (Phase == 3)
    {
        if (Elapsed < .10f) Peak = FMath::Max(Peak,Move->Velocity.Size2D());
        if (Elapsed > .5f) { Check(Peak > 0 && Move->Velocity.Size2D() < 1,TEXT("inertial braking then stop")); GroundZ=Host->GetActorLocation().Z; Host->Jump(); Advance(); }
        return;
    }
    if (Phase == 4)
    {
        Peak = FMath::Max(Peak,Host->GetActorLocation().Z-GroundZ);
        if (Elapsed > .8f) { Check(Peak > 53 && Peak < 65 && Move->IsMovingOnGround(),FString::Printf(TEXT("jump peak %.1fcm; landed within 0.8s"),Peak)); Host->StopJumping(); Advance(); }
        return;
    }
    if (Phase >= 5 && Phase <= 24)
    {
        const int32 Index = Phase-5, Height=(Index+1)*10;
        const float X=(Index/5)*650.f, Y=(Index%5-2)*650.f;
        if (LocalPhase != Phase)
        {
            LocalPhase = Phase;
            Move->StopMovementImmediately(); Move->SetMovementMode(MOVE_Walking); Move->bWalkHeld=false; Move->bClimbHeld=Height>80;
            Host->StopJumping(); Host->TeleportTo(FVector(X-(Height>150 ? 140:175),Y,95),FRotator::ZeroRotator);
            Host->Controller->SetControlRotation(FRotator::ZeroRotator);

        }
        if (Height > 150 && Elapsed >= .15f && Elapsed-DeltaTime < .15f) Host->Jump();
        if ((Height <= 150 || Elapsed >= .15f) && Host->GetActorLocation().X < X-30) Host->AddMovementInput(FVector(1,0,0));
        SawTraversal |= Move->IsTraversing();
        if (Elapsed > 2.1f)
        {
            const float Feet=Host->GetActorLocation().Z-92.5f;
            Check(FMath::IsNearlyEqual(Feet,Height+2.1f,4) && Move->IsMovingOnGround(),FString::Printf(TEXT("block %dcm reached (feet %.1fcm)"),Height,Feet));
            Check(SawTraversal == (Height>30),FString::Printf(TEXT("block %dcm traversal threshold"),Height));
            Host->StopJumping(); Move->bClimbHeld=false; Advance();
        }
        return;
    }
    if (Phase == 25 || Phase == 26)
    {
        if (LocalPhase != Phase)
        {
            LocalPhase=Phase;
            // The host finished its last case on the 200cm block. Clear that
            // landing before testing the client: FindLedge must reject pawns.
            if(Phase==25)
            {
                Host->TeleportTo(FVector(-700,-1900,95),FRotator::ZeroRotator,false,true);
                Move->StopMovementImmediately();Move->SetMovementMode(MOVE_Walking);Host->ResetJumpState();
            }
            Client->TeleportTo(FVector(-700,1900,95),FRotator::ZeroRotator);
            Client->Controller->SetControlRotation(FRotator::ZeroRotator);
        }
        if (Elapsed > 2)
        {
            const auto* Remote = Cast<UERMovement>(Client->GetCharacterMovement());
            Check(FMath::IsNearlyEqual(Remote->Velocity.Size2D(),Phase==25 ? 1400.f/3.6f : 750.f/3.6f,2) && Remote->bWalkHeld==(Phase==26),Phase==25 ? TEXT("remote client run replicated") : TEXT("remote client walk intent replicated"));
            Advance();
        }
        return;
    }
    if (Phase >= 27 && Phase <= 29)
    {
        const int32 Height = Phase == 27 ? 80 : (Phase == 28 ? 150 : 200);
        const float X = Phase == 27 ? 650.f : (Phase == 28 ? 1300.f : 1950.f);
        const float Y = Phase == 27 ? 0.f : 1300.f;
        if (LocalPhase != Phase)
        {
            LocalPhase=Phase;
            Client->GetCharacterMovement()->StopMovementImmediately();
            Client->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            Client->TeleportTo(FVector(X-(Height>150 ? 140:175),Y,95),FRotator::ZeroRotator);
            Client->ForceNetUpdate();
        }
        SawTraversal |= Cast<UERMovement>(Client->GetCharacterMovement())->IsTraversing();
        if (Elapsed > 3.1f)
        {
            const float Feet=Client->GetActorLocation().Z-92.5f;
            Check(FMath::IsNearlyEqual(Feet,Height+2.1f,4) && Client->GetCharacterMovement()->IsMovingOnGround() && SawTraversal,
                FString::Printf(TEXT("remote block %dcm (feet %.1fcm)"),Height,Feet));
            Advance();
        }
        return;
    }
    if (Phase == 30)
    {
        Move->StopMovementImmediately(); Move->SetMovementMode(MOVE_Walking); Move->bClimbHeld=false;
        Host->TeleportTo(FVector(1300-150,650,95),FRotator::ZeroRotator); // 140cm block, no climb input
        FVector Destination; float Height;
        Check(!Move->FindLedge(Destination,Height),TEXT("high ledge requires climb input"));
        Host->TeleportTo(FVector(1950-150,1300,95),FRotator::ZeroRotator); Move->bClimbHeld=true;
        Check(!Move->FindLedge(Destination,Height),TEXT("200cm unreachable from standing shoulders"));
        Host->TeleportTo(FVector(650-150,0,94.6f),FRotator::ZeroRotator);
        Check(Move->FindLedge(Destination,Height),TEXT("80cm ledge clear before adding ceiling"));
        auto* Roof = GetWorld()->SpawnActor<AActor>();
        auto* Box = NewObject<UBoxComponent>(Roof);
        Roof->SetRootComponent(Box); Box->SetBoxExtent(FVector(100,100,10));
        Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
        Roof->SetActorLocation(FVector(650,0,230));
        Check(!Move->FindLedge(Destination,Height),TEXT("standing body clearance rejects low ceiling"));
        Roof->Destroy(); Host->Crouch();
        Advance(); return;
    }
    if (Phase == 31)
    {
        if (Elapsed > .55f)
        {
            FVector Eye; FRotator View; Host->GetActorEyesViewPoint(Eye,View);
            Check(Host->bIsCrouched && FMath::IsNearlyEqual(Host->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),62.5f) && FVector::Dist(Eye,Host->Camera->GetComponentLocation()) < 1,
                TEXT("125cm crouch; camera and server aim agree"));
            Host->UnCrouch(); Advance();
        }
        return;
    }
    if (Phase == 32)
    {
        if (Elapsed > .55f)
        {
            FVector Eye; FRotator View; Host->GetActorEyesViewPoint(Eye,View);
            Check(!Host->bIsCrouched && FMath::IsNearlyEqual(Host->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),92.5f) && FVector::Dist(Eye,Host->Camera->GetComponentLocation()) < 1,
                TEXT("185cm standing restored; camera and server aim agree"));
            Advance();
        }
        return;
    }
    if (Phase >= 33 && Phase <= 39)
    {
        auto* Remote = Cast<UERMovement>(Client->GetCharacterMovement());
        if (LocalPhase != Phase)
        {
            LocalPhase = Phase;
            if (Phase == 33 || Phase == 34 || Phase == 36 || Phase == 37)
            {
                Client->TeleportTo(FVector(-900,1900,95), FRotator::ZeroRotator);
                Client->Controller->SetControlRotation(FRotator::ZeroRotator);
            }
            if (Phase == 37) Move->bProneWanted = true;
            if (Phase == 39) Move->bProneWanted = false;
        }
        if (Phase == 33)
        {
            if (Elapsed > 1.5f && Peak == 0)
            {
                Check(Remote->IsSprinting() && FMath::IsNearlyEqual(Remote->Velocity.Size2D(),2500.f/3.6f,2),TEXT("remote double-W intent: 25 km/h sprint"));
                auto* Weapon=Client->Weapon.Get();const float SprintCone=Weapon->GetSpreadDegrees();
                const FVector SprintVelocity=Remote->Velocity;Remote->Velocity=SprintVelocity.GetSafeNormal2D()*(1400.f/3.6f);
                const float RunCone=Weapon->GetSpreadDegrees();Remote->Velocity=SprintVelocity;
                Check(SprintCone>RunCone+1.4f,TEXT("actual remote sprint adds real dispersion beyond ordinary running"));
                const uint32 Shots=Weapon->AuthorizedShotCount;
                Client->ServerStartFire();Client->ServerStopFire();
                Check(Weapon->AuthorizedShotCount==Shots+1 && FMath::IsNearlyEqual(Weapon->LastShotSpreadDegrees,SprintCone,.001f),
                    TEXT("actual sprint shot samples the same widened cone as the HUD"));
                Peak=1;
            }
            if (Elapsed > 15.8f) { Check(Peak==1 && !Remote->IsSprinting() && Remote->GetRecoveryRemaining()>18 && Remote->GetRecoveryRemaining()<20 && Remote->GetStaminaFraction()>0 && Remote->GetStaminaFraction()<.3f,TEXT("sprint ends at 15 seconds; stamina refills over 20 seconds")); Advance(); }
        }
        else if (Phase == 34 && Elapsed > 20.5f)
        {
            Check(!Remote->IsSprinting() && Remote->GetRecoveryRemaining()==0 && Remote->GetStaminaFraction()==1 && Remote->Velocity.Size2D()<420,TEXT("held sprint cannot restart after cooldown")); Advance();
        }
        else if (Phase == 35 && Elapsed > .5f) Advance();
        else if (Phase == 36 && Elapsed > 1)
        {
            Check(Remote->IsSprinting(),TEXT("fresh remote gesture restarts recovered sprint"));
            SprintStartedAt = Remote->Locomotion.SprintUntil - UERMovement::MaxSprintDuration;
            Advance();
        }
        else if (Phase == 37)
        {
            // Let physics publish the low ceiling while already prone, before
            // the next phase requests standing on either peer.
            if(Elapsed-DeltaTime<1.5f && Elapsed>=1.5f)
            {
                const FVector P=Client->GetActorLocation();
                Fixture->UpdateInstanceTransform(25,FTransform(FQuat::Identity,FVector(P.X,P.Y,90),FVector(5,5,.2)),false,true,true);
            }
            if (Elapsed > .5f && Peak == 0) { Check(Remote->Locomotion.HalfHeight > 34 && Remote->Locomotion.HalfHeight < 92.5f,TEXT("remote prone transition takes time")); Peak=1; }
            if (Elapsed > 1.8f)
            {
                FVector Eye; FRotator View; Client->GetActorEyesViewPoint(Eye,View);
                FHitResult BodyHit;
                const FVector BodyCenter = Client->GetActorLocation() + Client->GetActorQuat().RotateVector(FVector(100,0,-9));
                const FVector Across = Client->GetActorRightVector()*100;
                GetWorld()->LineTraceSingleByChannel(BodyHit,BodyCenter-Across,BodyCenter+Across,ECC_GameTraceChannel1);
                Check(BodyHit.GetActor()==Client && BodyHit.GetComponent()==Client->ProneHitVolume,TEXT("prone head/body beyond movement capsule remains hittable"));
                Check(Peak==1 && Remote->IsProne() && FMath::IsNearlyEqual(Remote->Locomotion.HalfHeight,34.f,.1f),TEXT("remote prone capsule reaches 68cm"));
                Check(FVector::Dist(Eye,Client->Camera->GetComponentLocation())<1,TEXT("prone camera equals authoritative eye"));
                const double SprintDuration = Remote->Locomotion.SprintUntil - SprintStartedAt;
                Check(!Remote->IsSprinting() && SprintDuration > 0 && SprintDuration < UERMovement::MaxSprintDuration &&
                    FMath::IsNearlyEqual(Remote->Locomotion.RecoverUntil - Remote->Locomotion.SprintUntil, UERMovement::RecoveryRatio * SprintDuration, .001),
                    TEXT("posture interruption: recovery is twice the actual sprint duration"));
                Advance();
            }
        }
        else if (Phase == 38 && Elapsed > 1.5f)
        {

            Check(Remote->IsProne() && FMath::IsNearlyEqual(Remote->Locomotion.HalfHeight,34.f,.1f),TEXT("blocked ceiling prevents standing from prone"));
            Fixture->UpdateInstanceTransform(25,FTransform(FQuat::Identity,FVector(-100000,0,90),FVector(5,5,.2)),false,true,true);Advance();
        }
        else if (Phase == 39 && Elapsed > 1.8f)
        {
            if (Peak == 0) { Check(!Remote->IsProne() && FMath::IsNearlyEqual(Remote->Locomotion.HalfHeight,92.5f,.1f),TEXT("remote stands gradually when ceiling clears")); Peak=1; }
            if (Remote->GetRecoveryRemaining()==0) Advance();
        }
        return;
    }
    if (Phase >= 40 && Phase <= 43)
    {
        auto* Remote = Cast<UERMovement>(Client->GetCharacterMovement());
        if (Phase == 40 && Elapsed > .85f && Peak == 0)
        {
            Check(Remote->IsSprinting() && Remote->Velocity.Size2D()>350,TEXT("InputKey double W reaches real sprint/movement bindings"));
            SprintStartedAt = Remote->Locomotion.SprintUntil - UERMovement::MaxSprintDuration; Peak=1;
        }
        if (Elapsed > 1.8f)
        {
            if (Phase == 40)
            {
                const double SprintDuration = Remote->Locomotion.SprintUntil - SprintStartedAt;
                Check(Peak==1 && !Remote->IsSprinting() && SprintDuration > 0 && SprintDuration < UERMovement::MaxSprintDuration &&
                    FMath::IsNearlyEqual(Remote->Locomotion.RecoverUntil - Remote->Locomotion.SprintUntil, UERMovement::RecoveryRatio * SprintDuration, .001),
                    TEXT("InputKey W release: recovery is twice the actual sprint duration"));
            }
            else Check(Phase == 42 ? !Remote->IsProne() && Remote->Locomotion.HalfHeight>92 : Remote->IsProne() && Remote->Locomotion.HalfHeight<35,
                Phase == 41 ? TEXT("InputKey double C reaches prone binding") : (Phase == 42 ? TEXT("InputKey single C rises from prone") : TEXT("InputKey double Ctrl reaches prone binding")));
            Advance();
        }
        return;
    }
    if (Phase >= 44 && Phase <= 47)
    {
        if (LocalPhase != Phase)
        {
            LocalPhase=Phase;
            Host->TeleportTo(FVector(-900,-1900,95),FRotator::ZeroRotator);
            Host->Controller->SetControlRotation(FRotator::ZeroRotator);
        }
        if (FParse::Param(FCommandLine::Get(),TEXT("ERStaminaGallery")) && Elapsed-DeltaTime<.3f && Elapsed>=.3f)
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Testing/Stamina/Phase%d.png"),Phase),true,false);
        Move->bSprintHeld=Phase==44 || Phase==46;
        if (Move->bSprintHeld) Host->AddMovementInput(FVector(1,0,0));
        if (Phase==44 && Elapsed>6.f)
        {
            Check(Move->IsSprinting() && Move->GetStaminaFraction()>.55f && Move->GetStaminaFraction()<.65f,
                TEXT("six seconds of sprint consumes forty percent stamina"));
            Advance();
        }
        else if (Phase==45 && Elapsed>.4f)
        {
            Check(!Move->IsSprinting() && Move->GetStaminaFraction()>.6f && Move->GetStaminaFraction()<.75f,
                TEXT("short rest restores only part of the spent stamina"));
            Advance();
        }
        else if (Phase==46 && Elapsed>1.f)
        {
            Check(Move->IsSprinting() && Move->GetSprintRemaining()>8.f && Move->GetSprintRemaining()<10.f,
                TEXT("fresh gesture resumes partial stamina without resetting to fifteen seconds"));
            Advance();
        }
        else if (Phase==47 && Elapsed>20.5f)
        {
            Check(!Move->IsSprinting() && Move->GetStaminaFraction()==1.f,
                TEXT("interrupted partial sprint refills fully while resting"));
            Advance();
        }
        return;
    }
    if (Phase>=48 && Phase<=53)
    {
        auto* Remote=Cast<UERMovement>(Client->GetCharacterMovement());
        if (LocalPhase!=Phase)
        {
            LocalPhase=Phase;
            if (Phase==48 || Phase==50 || Phase==51 || Phase==52)
                Client->TeleportTo(FVector(-900,1900,Remote->Locomotion.HalfHeight+2.1f),FRotator::ZeroRotator);
        }
        if (Phase==51 && Peak==0 && Elapsed>.5f)
        {
            Check(!Remote->IsSprinting(),TEXT("held E without forward movement does not sprint")); Peak=1;
        }
        if (Elapsed>(Phase==48?3.f:(Phase==49 || Phase==53?.6f:1.5f)))
        {
            const bool Sprint=Phase!=49 && Phase!=53;
            Check(Remote->IsSprinting()==Sprint && (!Sprint || Remote->GetStaminaFraction()<1.f),
                Phase==48?TEXT("InputKey held E starts sprint during normal W movement"):
                Phase==49?TEXT("InputKey releasing E stops sprint while W remains held"):
                Phase==50?TEXT("InputKey E held before W starts sprint after gesture window"):
                Phase==51?TEXT("InputKey forward resumes sprint while E remains held"):
                Phase==52?TEXT("InputKey releasing E preserves simultaneous double-W sprint"):
                TEXT("InputKey releasing both sprint controls clears intent"));
            Advance();
        }
        return;
    }
    UE_LOG(LogTemp,Display,TEXT("ER_MOVEMENT_COMPLETE %s"),Failed ? TEXT("FAIL") : TEXT("PASS"));
    FPlatformMisc::RequestExit(false);
#endif
}
