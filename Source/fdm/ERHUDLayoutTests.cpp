#include "ERHUDLayout.h"
#include "ERPlayerController.h"
#include "ERRecon.h"
#include "ERCharacter.h"
#include "ERWeapon.h"
#include "ERMatch.h"
#include "Engine/Canvas.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

// Opt-in rendered resize regression: exercise the same live viewport and DPI
// path used when changing window size, including the scoreboard and Slate FPS.
void AERHUD::TickHUDLayoutGallery()
{
#if !UE_BUILD_SHIPPING
    if (!FParse::Param(FCommandLine::Get(),TEXT("ERHUDGallery"))) return;
    auto* PC=Cast<AERPlayerController>(PlayerOwner);
    // With -ERMatchCountdown=<s>, push travel and jump input through the frozen
    // countdown and report how far the pawn actually moved once it ends.
    if(auto* GS=GetWorld()->GetGameState<AERGameState>(); GS && PC)
    {
        auto* C=Cast<AERCharacter>(PC->GetPawn());
        if(GS->IsPreMatchCountdown() && C)
        {
            if(!bGalleryCountdownSeen){bGalleryCountdownSeen=true;GalleryCountdownStart=C->GetActorLocation();}
            C->AddMovementInput(C->GetActorForwardVector(),1.f);C->Jump();
        }
        else if(bGalleryCountdownSeen && !bGalleryCountdownReported && GS->IsMatchInProgress() && C)
        {
            bGalleryCountdownReported=true;
            UE_LOG(LogTemp,Display,TEXT("ER_HUD_COUNTDOWN_FROZEN moved=%.2f"),FVector::Dist(GalleryCountdownStart,C->GetActorLocation()));
        }
    }
    if (!PC || !PC->Recon || !PC->Recon->Snapshot.Epoch || GetWorld()->GetTimeSeconds()<HUDGalleryNextAt) return;
    const FIntPoint Sizes[]={{640,360},{1280,720},{1920,1080},{3840,2160},{2560,1080},{1024,768},{720,1280}};
    const int32 Index=HUDGalleryStep/3, Phase=HUDGalleryStep%3;
    if (Index>=UE_ARRAY_COUNT(Sizes))
    {
        UE_LOG(LogTemp,Display,TEXT("ER_HUD_GALLERY_COMPLETE"));
        FPlatformMisc::RequestExit(false);
        return;
    }
    if (Phase==0)
    {
        if(FParse::Param(FCommandLine::Get(),TEXT("ERWeaponHUDGallery")))
        {
            if(auto* C=Cast<AERCharacter>(PC->GetPawn());C && C->HasAuthority() && !C->Inventory.IsEmpty())
            {
                const int32 Slot=Index%C->Inventory.Num();
                C->ServerSelectWeapon(Slot);
                if(Index>=5 && C->Weapon && C->Weapon->Data->Payload!=EERPayload::Melee)
                { C->Weapon->MagazineAmmo=0;C->Weapon->ReserveAmmo=0; }
                UE_LOG(LogTemp,Display,TEXT("ER_WEAPON_HUD_GALLERY index=%d slot=%d item=%s loaded=%d reserve=%d"),
                    Index,Slot,*GetNameSafe(C->Weapon?C->Weapon->Data.Get():nullptr),C->Weapon?C->Weapon->MagazineAmmo:-1,C->Weapon?C->Weapon->ReserveAmmo:-1);
            }
        }
        PC->bShowScoreboard=false;
        if (auto* PS = PC->GetPlayerState<AERPlayerState>())
            PS->KillStreak = {0, 1, 0, 1};
        // Sample kill feed rows: both teams, teamkill, headshot, explosive and a fall.
        if(auto* GS=GetWorld()->GetGameState<AERGameState>())
        {
            const FString Self=PC->PlayerState?PC->PlayerState->GetPlayerName():TEXT("HUD");
            const double Now=GetWorld()->GetTimeSeconds();
            auto Add=[&](const FString& Killer,EERTeam KillerTeam,const FString& Victim,EERTeam VictimTeam,int8 Key,bool Headshot,bool Fall)
            {
                FERKillFeedEntry& E=GS->KillFeed.AddDefaulted_GetRef();
                E.Killer=Killer;E.KillerTeam=KillerTeam;E.Victim=Victim;E.VictimTeam=VictimTeam;
                E.WeaponKey=Key;E.bHeadshot=Headshot;E.bFall=Fall;E.ReceivedAt=Now;
            };
            GS->KillFeed.Reset();
            Add(Self,EERTeam::Alpha,TEXT("Omega Rifleman"),EERTeam::Omega,4,true,false);
            Add(TEXT("Night Owl"),EERTeam::Omega,TEXT("Bot 3"),EERTeam::Alpha,6,false,false);
            Add(TEXT("Grenadier With A Long Name"),EERTeam::Omega,TEXT("Bot 7"),EERTeam::Omega,8,false,false);
            Add(TEXT("Bot 2"),EERTeam::Alpha,Self,EERTeam::Alpha,10,false,false);
            Add(FString(),EERTeam::None,TEXT("Clumsy"),EERTeam::Omega,-1,false,true);
        }
        PC->ConsoleCommand(FString::Printf(TEXT("r.SetRes %dx%dw"),Sizes[Index].X,Sizes[Index].Y));
    }
    else
    {
        const FERHUDLayout Layout(FVector2D(Canvas->SizeX,Canvas->SizeY));
        UE_LOG(LogTemp,Display,TEXT("ER_HUD_GALLERY index=%d scoreboard=%d viewport=%dx%d scale=%.3f radius=%.2f edge=%.2f,%.2f"),
            Index,Phase==2,Canvas->SizeX,Canvas->SizeY,Layout.Scale,Layout.MinimapRadius(),Layout.Edge().X,Layout.Edge().Y);
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Testing/HUD/%d-%s.png"),Index,Phase==2?TEXT("scoreboard"):TEXT("hud")),true,false);
        PC->bShowScoreboard=Phase==1;
    }
    ++HUDGalleryStep;
    HUDGalleryNextAt=GetWorld()->GetTimeSeconds()+2;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FERHUDLayoutTest,"EclipseRecoil.Player.HUDViewportLayout",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FERHUDLayoutTest::RunTest(const FString&)
{
    for(float SpawnYaw : {0.f,90.f,180.f,-90.f})
    {
        TestTrue(TEXT("Enemy base bearing is up at respawn for either team"),FERHUDLayout::SpawnBearing(SpawnYaw,SpawnYaw).Equals(FVector2D(0,-1),.001));
        TestTrue(TEXT("Fixed bearing rotates right when looking left"),FERHUDLayout::SpawnBearing(SpawnYaw,SpawnYaw-90).Equals(FVector2D(1,0),.001));
        TestTrue(TEXT("Fixed bearing is behind when looking back"),FERHUDLayout::SpawnBearing(SpawnYaw,SpawnYaw+180).Equals(FVector2D(0,1),.001));
    }
    const FERHUDLayout Base(FVector2D(1280,720));
    for (const float Factor : {.5f,1.f,1.5f,2.f,3.f,4.f})
    {
        const FERHUDLayout L(Base.Viewport*Factor);
        TestTrue(TEXT("Minimap grows proportionally below and above the old pixel caps"),FMath::IsNearlyEqual(L.MinimapRadius(),Base.MinimapRadius()*Factor));
        TestTrue(TEXT("Corner offsets stay proportional on both axes"),L.Edge().Equals(Base.Edge()*Factor));
        TestTrue(TEXT("Minimap keeps the same normalized placement"),L.MinimapCenter().Equals(Base.MinimapCenter()*Factor));
    }
    for (const FVector2D Size : {FVector2D(1024,768),FVector2D(2560,1080),FVector2D(720,1280)})
    {
        const FERHUDLayout L(Size);
        TestTrue(TEXT("Horizontal inset is a fraction of width"),FMath::IsNearlyEqual(L.Edge().X/Size.X,40./1280));
        TestTrue(TEXT("Vertical inset is a fraction of height"),FMath::IsNearlyEqual(L.Edge().Y/Size.Y,40./720));
        const auto C=L.MinimapCenter(); const float R=L.MinimapRadius()+28*L.Scale;
        TestTrue(TEXT("Minimap top and right margins match at every aspect ratio"),FMath::IsNearlyEqual(C.Y-L.MinimapRadius(),Size.X-C.X-L.MinimapRadius(),.001));
        TestTrue(TEXT("Minimap, north marker and shadow fit every tested aspect ratio"),C.X-R>0 && C.Y-R>0 && C.X+R<Size.X && C.Y+R+80*L.Scale<Size.Y);
        TestTrue(TEXT("Vitals fit beside bottom-right ammo"),L.Edge().X+206*L.Scale<Size.X*.5);
    }
    return true;
}
#endif
