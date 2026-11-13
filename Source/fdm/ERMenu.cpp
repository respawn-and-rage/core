#include "ERLoadout.h"
#include "ERPlayerController.h"
#include "ERPerformanceSettings.h"
#include "EROnline.h"
#include "ERMatch.h"
#include "ERCharacter.h"
#include "ERMenuPreview.h"
#include "ERInterfaceStyle.h"
#include "ERMapCatalog.h"
#include "EROutdoorBackdrop.h"
#include "ERLobbyRules.h"
#include "ERWeapon.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Fonts/CompositeFont.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSafeZone.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
class SPlayerPreview : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SPlayerPreview) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AERMenuPreview>, Preview)
        SLATE_DEFAULT_SLOT(FArguments, Content)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args) { Preview=Args._Preview;ChildSlot[Args._Content.Widget]; }
    virtual ~SPlayerPreview() override { if(Preview.IsValid())Preview->SetDragging(false); }
    virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent& Event) override
    {
        if(Event.GetEffectingButton()!=EKeys::LeftMouseButton || !Preview.IsValid())return FReply::Unhandled();
        Preview->SetDragging(true);
        return FReply::Handled().CaptureMouse(SharedThis(this));
    }
    virtual FReply OnMouseMove(const FGeometry& Geometry,const FPointerEvent& Event) override
    {
        if(!HasMouseCapture() || !Preview.IsValid())return FReply::Unhandled();
        Preview->Rotate(-Event.GetCursorDelta().X/FMath::Max(.1f,Geometry.GetAccumulatedLayoutTransform().GetScale())*.35f);
        return FReply::Handled();
    }
    virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent& Event) override
    {
        if(Event.GetEffectingButton()!=EKeys::LeftMouseButton || !HasMouseCapture())return FReply::Unhandled();
        if(Preview.IsValid())Preview->SetDragging(false);
        return FReply::Handled().ReleaseMouseCapture();
    }
    virtual void OnMouseCaptureLost(const FCaptureLostEvent& Event) override
    { if(Preview.IsValid())Preview->SetDragging(false);SCompoundWidget::OnMouseCaptureLost(Event); }
private:
    TWeakObjectPtr<AERMenuPreview> Preview;
};
float ServerSettingDefault(const FERServerSetting& S,const AERCharacter* Catalog)
{
    if(!S.AssetDefault)return S.Default;
    if(!S.Weapon.IsNone() && Catalog)
        for(const auto& D:Catalog->ReferenceArsenal)if(D && D->GetFName()==S.Weapon)
            return ERServer::ReadNumber(D,D->GetClass(),S.Property)/S.Factor;
    if(S.Key.ToString().StartsWith(TEXT("Kit.")))
    {
        TArray<FString> Parts;S.Key.ToString().ParseIntoArray(Parts,TEXT("."));
        if(Parts.Num()==4)
        {
            const auto Kit=ERLoadouts::Resolve(FName(*Parts[1]),FName(*Parts[2]),false,Catalog);
            if(Parts[3]==TEXT("PrimaryLoaded"))return Kit.Items[0].Loaded;
            if(Parts[3]==TEXT("PrimaryReserve"))return Kit.Items[0].Reserve;
            if(Parts[3]==TEXT("SecondaryLoaded"))return Kit.Items[1].Loaded;
            if(Parts[3]==TEXT("SecondaryReserve"))return Kit.Items[1].Reserve;
            const int32 Key=Parts[3]==TEXT("HE")?8:Parts[3]==TEXT("Smoke")?9:10;
            for(const auto& Item:Kit.Items)if(Item.WeaponKey==Key)return Item.Loaded+Item.Reserve;
        }
    }
    return S.Default;
}
FString LoadoutVariantName(FName Variant)
{
    return Variant==FName("gunner")?TEXT("Gunner"):Variant==FName("offense")?TEXT("Offense"):TEXT("Defense");
}
FSlateFontInfo MenuFont(int32 Size)
{
    return ERInterface::Font(Size);
}

class SControlKeyButton : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SControlKeyButton) {}
        SLATE_ATTRIBUTE(FText, KeyText)
        SLATE_ARGUMENT(TFunction<void(const FKey&)>, OnSelected)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        KeyText = Args._KeyText;
        OnSelected = Args._OnSelected;
        ChildSlot[SNew(SButton).ContentPadding(FMargin(10, 3))
            .OnClicked_Lambda([this]()
            {
                bSelecting = true;
                return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::SetDirectly);
            })
            [SNew(STextBlock).Font(MenuFont(22)).Text_Lambda([this]()
            {
                return bSelecting ? FText::FromString(TEXT("Press a key…")) : KeyText.Get();
            })]];
    }

    bool SupportsKeyboardFocus() const override { return true; }
    void OnFocusLost(const FFocusEvent& Event) override
    {
        bSelecting = false;
        SCompoundWidget::OnFocusLost(Event);
    }
    FReply OnPreviewKeyDown(const FGeometry&, const FKeyEvent&) override
    {
        return bSelecting ? FReply::Handled() : FReply::Unhandled();
    }
    FReply OnKeyUp(const FGeometry&, const FKeyEvent& Event) override
    {
        return Select(Event.GetKey());
    }
    FReply OnPreviewMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
    {
        return Select(Event.GetEffectingButton());
    }
    FReply OnMouseWheel(const FGeometry&, const FPointerEvent& Event) override
    {
        if (Event.GetWheelDelta() == 0.f) return FReply::Unhandled();
        return Select(Event.GetWheelDelta() > 0.f ? EKeys::MouseScrollUp : EKeys::MouseScrollDown);
    }

private:
    FReply Select(const FKey& Key)
    {
        if (!bSelecting) return FReply::Unhandled();
        bSelecting = false;
        OnSelected(Key);
        return FReply::Handled();
    }

    bool bSelecting = false;
    TAttribute<FText> KeyText;
    TFunction<void(const FKey&)> OnSelected;
};

TSharedRef<STextBlock> Text(const FString& Value, int32 Size = 24)
{
    return SNew(STextBlock).Text(FText::FromString(Value)).Font(MenuFont(Size))
        .ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(2, 2)).ShadowColorAndOpacity(FLinearColor::Black).AutoWrapText(true);
}
TSharedRef<SWidget> Button(const FString& Label, TFunction<FReply()> Action, bool Main = false)
{
    if (!Main)
        return SNew(SButton).ContentPadding(FMargin(18, 10)).OnClicked_Lambda(MoveTemp(Action))[Text(Label, 25)];
    // Keep the background artwork visible in every state. Text inherits the
    // button foreground, so hover/press no longer get masked by fixed white.
    static const FButtonStyle MainStyle = FButtonStyle(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
        .SetNormalForeground(FLinearColor::White)
        .SetHoveredForeground(FLinearColor(1.f, .68f, .22f))
        .SetPressedForeground(FLinearColor(1.f, .24f, .045f))
        .SetNormalPadding(FMargin(0))
        .SetPressedPadding(FMargin(4, 2, -4, -2));
    auto Entry = Text(Label, 54);
    Entry->SetColorAndOpacity(FSlateColor::UseForeground());
    return SNew(SButton).ButtonStyle(&MainStyle).Cursor(EMouseCursor::Hand)
        .ContentPadding(FMargin(12, 9)).OnClicked_Lambda(MoveTemp(Action))[Entry];
}
void Add(TSharedRef<SVerticalBox> Panel, TSharedRef<SWidget> Widget)
{
    Panel->AddSlot().AutoHeight().Padding(0, 5)[Widget];
}
// Reflow whole label/control groups, preserving keyboard order and keeping
// each field together. Cached local width includes Slate DPI scaling.
TFunction<TSharedRef<SVerticalBox>()> Columns(TSharedRef<SVerticalBox> Panel)
{
    auto Grid=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(24,8));
    Add(Panel,Grid);
    TWeakPtr<SVerticalBox> WeakPanel=Panel;
    return [Grid,WeakPanel]()
    {
        auto Cell=SNew(SVerticalBox);
        Grid->AddSlot().VAlign(VAlign_Top)
            [SNew(SBox).WidthOverride_Lambda([WeakPanel]()
            {
                const auto Owner=WeakPanel.Pin();
                const float Width=Owner.IsValid() ? Owner->GetCachedGeometry().GetLocalSize().X:0.f;
                return Width>0 ? (Width>=900 ? (Width-24)/2:Width):440.f;
            })[Cell]];
        return Cell;
    };
}
void Choice(TSharedRef<SVerticalBox> Panel, const FString& Title, TArray<FString> Labels,
    TFunction<int32()> Get, TFunction<void(int32)> Set)
{
    Add(Panel, Text(Title, 22));
    auto Items = SNew(SVerticalBox);
    for (int32 I = 0; I < Labels.Num(); ++I)
        Add(Items, Button(Labels[I], [I, Set]() { Set(I); FSlateApplication::Get().DismissAllMenus(); return FReply::Handled(); }));
    Add(Panel, SNew(SComboButton)
        .ButtonContent()[SNew(STextBlock).Font(MenuFont(25)).AutoWrapText(true).Text_Lambda([Labels, Get]() { return FText::FromString(Labels[FMath::Clamp(Get(), 0, Labels.Num()-1)]); })]
        .MenuContent()[SNew(SBox).MaxDesiredHeight(420)[SNew(SScrollBox) + SScrollBox::Slot()[Items]]]);
}
}
void AERPlayerController::ERMenu(int32 Page) { OpenMenuPage(FMath::Clamp(Page, 0, 11)); }
void AERPlayerController::OpenMenuPage(int32 Page)
{
    if(Page==10 && MenuPage!=10)ServerRulesParent=MenuPage;
    if (Page == 11) ControlBindingFeedback = FText::GetEmpty();
    if (Page == 5) Page = 6; // Compatibility with existing menu commands.
    if ((Page == 5 || Page == 6 || Page == 7) && MenuPage != 5 && MenuPage != 6 && MenuPage != 7 && MenuPage != 9) SubmenuParent = MenuPage;
    if(Page==6 && MenuPage!=6 && MenuPage!=9)
    {
        const auto* GI=GetGameInstance<UERGameInstance>();const auto* PS=(!IsMenuLevel() || IsInLobby())?GetPlayerState<AERPlayerState>():nullptr;
        const auto Kit=ERLoadouts::Resolve(PS?PS->LoadoutPreset:GI->LoadoutPreset,PS?PS->LoadoutVariant:GI->LoadoutVariant);
        DraftPreset=Kit.Preset;DraftVariant=Kit.Variant;
    }
    MenuPage = Page;
    ShowMenu(true);
}
void AERPlayerController::ShowMenu(bool bShow)
{
    if(bShow)if(auto* C=Cast<AERCharacter>(GetPawn()))C->SetFireInputHeld(false);
    if (!IsLocalController() || !GetWorld()->GetGameViewport()) return;
    ResetSprintInput(); LastCrouchTap = -1;
    FireReleased(); WalkReleased(); CrouchReleased(); JumpReleased(); bShowScoreboard = false;
    if (auto* C = Cast<AERCharacter>(GetPawn()); C && C->Weapon) C->Weapon->ServerClearScope();
    bShowingLobby = IsInLobby();
    bMenuOpen = bShow; bShowMouseCursor = bShow;
    UpdateMenuMusic();
    SetIgnoreMoveInput(false); SetIgnoreLookInput(false);
    if (MenuWidget.IsValid()) { GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(MenuWidget.ToSharedRef()); MenuWidget.Reset(); }
    if (MenuPreview && (!bShow || MenuPage!=6)) { MenuPreview->Destroy(); MenuPreview = nullptr; }
    PreviewBrush.Reset();
    MenuFocusWidget.Reset();
    bPendingWindowInput = true;
    if (!bShow) { ApplyWindowInputMode(); return; }
    auto* GI = GetGameInstance<UERGameInstance>(); if (!GI) return;
    const bool Main = IsMenuLevel();
    const bool Lobby = bShowingLobby;
    if (Lobby) GI->LastError.Empty();
    const bool Home = MenuPage == 0 && !Lobby;
    auto Panel = SNew(SVerticalBox);
    TSharedPtr<SWidget> Focus;
    TSharedPtr<SWidget> PlayerPreview;
    TSharedPtr<SWidget> OfflineStart;
    auto PlayerFooter=SNew(SVerticalBox);
    auto Go = [this](int32 Page) { return [this, Page]() { OpenMenuPage(Page); return FReply::Handled(); }; };
    auto* Online = GI->GetSubsystem<UEROnline>();
    auto Back = [this, GI, Online]() { Online->Cancel(); GI->CancelLANSearch(); OpenMenuPage(MenuPage==10 ? ServerRulesParent : MenuPage == 5 || MenuPage == 6 || MenuPage == 7 ? SubmenuParent : MenuPage == 9 ? 6 : MenuPage == 8 || MenuPage == 11 ? 4 : 0); return FReply::Handled(); };
    auto SaveSupplies = [this, GI]() { GI->SaveProfile(); ServerSetSupplies(GI->SpareMagazines, GI->HECount, GI->SmokeCount, GI->StunCount); };
    TFunction<TSharedRef<SVerticalBox>()> Field=[Panel]() { return Panel; };
    auto IntChoice = [&Field](const FString& Label, TArray<FString> Labels, int32* Value)
    { Choice(Field(), Label, Labels, [Value]() { return *Value; }, [Value](int32 I) { *Value = I; }); };
    TArray<FString> Counts; for (int32 I = 0; I <= 20; ++I) Counts.Add(FString::FromInt(I));
    const auto* Catalog = SkinCatalog ? SkinCatalog->GetDefaultObject<AERCharacter>() : nullptr;
    if (Home)
    {
        if (Main)
        {
            Focus = Button(TEXT("Play Offline"), Go(1), true); Add(Panel, Focus.ToSharedRef());
            Add(Panel, Button(TEXT("Play Online"), Go(3), true));
            Add(Panel, Button(TEXT("Start Server"), [this, GI]() { GI->AlphaBots = GI->OmegaBots = 0; OpenMenuPage(2); return FReply::Handled(); }, true));
        }
        else
        {
            Focus = Button(TEXT("Resume"), [this]() { ShowMenu(false); return FReply::Handled(); }, true); Add(Panel, Focus.ToSharedRef());
            Add(Panel, Button(TEXT("Player"), Go(6), true));
            Add(Panel, Button(TEXT("Server rules"),Go(10)));
            Add(Panel, Button(TEXT("Main Menu"), [GI]() { GI->ReturnToMenu(); return FReply::Handled(); }, true));
        }
        if(Main)Add(Panel, Button(TEXT("Player"), Go(6), true));
        Add(Panel, Button(TEXT("Setup"), Go(4), true));
        Add(Panel, Button(TEXT("Credits"), Go(7), true));
        Add(Panel, Button(TEXT("Exit Game"), [GI]() { GI->QuitGame(); return FReply::Handled(); }, true));
    }
    else
    {
        const TCHAR* Titles[] = {TEXT("MULTIPLAYER LOBBY"), TEXT("PLAY OFFLINE"), TEXT("START SERVER"), TEXT("PLAY ONLINE"), TEXT("SETUP"), TEXT("PLAYER"), TEXT("PLAYER"), TEXT("CREDITS"), TEXT("PERFORMANCE"), TEXT("CHANGE TEAM"),TEXT("SERVER GAMEPLAY SETTINGS"), TEXT("CONTROLS")};
        Focus = Button(MenuPage == 0 ? TEXT("Leave Server") : TEXT("Back"), MenuPage == 0 ? TFunction<FReply()>([GI]() { GI->ReturnToMenu(); return FReply::Handled(); }) : TFunction<FReply()>(Back));
        if (MenuPage != 7)
        {
            Add(Panel, Text(Titles[FMath::Clamp(MenuPage, 0, 11)], 36));
            if(MenuPage!=6 && MenuPage!=10)Add(Panel, Focus.ToSharedRef());
        }
        if (MenuPage == 0 && Lobby)
        {
            auto Status = SNew(STextBlock).Font(MenuFont(25)).AutoWrapText(true).Text_Lambda([this, GI]()
            {
                auto* GS = GetWorld()->GetGameState<AERGameState>();
                int32 Humans = 0, Ready = 0;
                if (GS) for (auto Item : GS->PlayerArray) if (const auto* PS = Cast<AERPlayerState>(Item); PS && !PS->IsABot() && !PS->IsInactive())
                { ++Humans; Ready += PS->bLobbyReady; }
                return FText::FromString(GS && GS->bLobbyStarting ? FString::Printf(TEXT("Starting %s · generating map..."), ERMatchModes::Label(GS->MatchMode)) : FString::Printf(TEXT("%s · %d / %d players    |    READY %d / %d needed"), GS && GS->bOnline ? TEXT("INTERNET") : TEXT("LAN"), Humans, GS ? GS->ServerMaxPlayers : GI->MaxPlayers, Ready, ERLobby::RequiredReady(Humans,ERServer::Get(GetWorld(),"Match.ReadyPercent"))));
            });
            Add(Panel, Status);
            Add(Panel,SNew(STextBlock).Font(MenuFont(22)).Text_Lambda([this]()
            {
                const int32 N=FMath::RoundToInt(ERServer::Get(GetWorld(),"Map.ExpectedTeamSize"));
                return FText::FromString(FString::Printf(TEXT("Map sizing: %d vs %d · target %d m² (±10%%) · fixed at server creation"),N,N,N*1500));
            }));
            auto Body = SNew(SHorizontalBox);
            auto Teams = SNew(SVerticalBox);
            for (bool Alpha : {true, false})
            {
                Add(Teams, SNew(STextBlock).Font(MenuFont(28)).ColorAndOpacity(Alpha ? FLinearColor(.35f,.65f,1) : FLinearColor(1,.5f,.22f)).Text(FText::FromString(Alpha ? TEXT("ALPHA") : TEXT("OMEGA"))));
                Add(Teams, SNew(SBox).MaxDesiredHeight(175)[SNew(SScrollBox) + SScrollBox::Slot()[SNew(STextBlock).Font(MenuFont(20)).AutoWrapText(true).Text_Lambda([this, Alpha]()
                {
                    FString Roster;
                    const auto* GS = GetWorld()->GetGameState<AERGameState>();
                    if (GS) for (auto Item : GS->PlayerArray) if (const auto* PS = Cast<AERPlayerState>(Item); PS && !PS->IsInactive() && (PS->Team == EERTeam::Alpha) == Alpha)
                    {
                        Roster += PS->GetPlayerName() + (PS->bLobbyReady ? TEXT("   READY") : TEXT("   preparing")) + TEXT("\n");
                    }
                    const int32 Bots = GS ? (Alpha ? GS->LobbyAlphaBots : GS->LobbyOmegaBots) : 0;
                    for (int32 I = 0; I < Bots; ++I) Roster += FString::Printf(TEXT("Bot %d   [test]\n"), I + 1);
                    return FText::FromString(Roster.IsEmpty() ? TEXT("No players yet") : Roster.TrimEnd());
                })]]);
            }
            Add(Teams, Text(TEXT("Vote for the match mode"), 25));
            auto ModeButtons = SNew(SHorizontalBox);
            for (EERMatchMode MatchMode : {EERMatchMode::TeamDeathmatch, EERMatchMode::SkullHunt})
            {
                const int32 Vote = int32(MatchMode);
                ModeButtons->AddSlot().AutoWidth().Padding(0, 0, 8, 0)
                    [SNew(SButton).ContentPadding(7)
                    .OnClicked_Lambda([this, Vote]() { ServerLobbyMatchModeVote(Vote); return FReply::Handled(); })
                    [SNew(STextBlock).Font(MenuFont(23)).Text_Lambda([this, MatchMode, Vote]()
                    {
                        int32 Votes = 0;
                        if (const auto* GS = GetWorld()->GetGameState<AERGameState>()) for (const APlayerState* Item : GS->PlayerArray)
                            if (const auto* PS = Cast<AERPlayerState>(Item); PS && PS->CanVoteInLobby() && PS->MatchModeVote == Vote) ++Votes;
                        const auto* PS = GetPlayerState<AERPlayerState>();
                        return FText::FromString(FString::Printf(TEXT("%s   (%d)%s"), ERMatchModes::Label(MatchMode),
                            Votes, PS && PS->MatchModeVote == Vote ? TEXT("  ✓") : TEXT("")));
                    })]];
            }
            Add(Teams, ModeButtons);
            Add(Teams, Text(TEXT("Collect enemy skulls for points. Clear friendly skulls to deny points."), 20));
            Add(Teams, Text(TEXT("Team Deathmatch wins ties and starts if nobody votes."), 20));
            auto Actions = SNew(SVerticalBox);
            Add(Actions, Button(TEXT("Player"), Go(6)));
            Add(Actions, Button(TEXT("Server rules"),Go(10)));
            Add(Actions, Text(ERServer::Rules(GetWorld()).IsStandard()?TEXT("Standard gameplay"):FString::Printf(TEXT("Custom gameplay · %d overrides"),ERServer::Rules(GetWorld()).Values.Num()),20));
            Add(Actions, Text(TEXT("Vote for the first map"), 25));
            for (int32 I = 0; I < ERMaps::All().Num(); ++I)
            {
                if (!ERMaps::Enabled(I,&ERServer::Rules(GetWorld()))) continue;
                Add(Actions, SNew(SButton).ContentPadding(7).OnClicked_Lambda([this, I]() { ServerLobbyVote(I); return FReply::Handled(); })
                    [SNew(STextBlock).Font(MenuFont(25)).AutoWrapText(true).Text_Lambda([this, I]()
                    {
                        int32 Votes = 0;
                        if (const auto* GS = GetWorld()->GetGameState<AERGameState>()) for (auto Item : GS->PlayerArray)
                            if (const auto* PS = Cast<AERPlayerState>(Item); PS && !PS->IsABot() && !PS->IsInactive() && PS->MapVote == I) ++Votes;
                        const auto* PS = GetPlayerState<AERPlayerState>();
                        return FText::FromString(FString::Printf(TEXT("%s   (%d)%s"), ERMaps::At(I).Label, Votes, PS && PS->MapVote == I ? TEXT("  YOUR VOTE") : TEXT("")));
                    })]);
            }
            
            Add(Actions, Text(TEXT("Ties, including no votes: random choice among the leading maps."), 20));
            Add(Actions, SNew(SButton).ContentPadding(12).OnClicked_Lambda([this]() { const auto* PS = GetPlayerState<AERPlayerState>(); if (PS) ServerLobbyReady(!PS->bLobbyReady); return FReply::Handled(); })
                [SNew(STextBlock).Font(MenuFont(32)).Text_Lambda([this]() { const auto* PS = GetPlayerState<AERPlayerState>(); return FText::FromString(PS && PS->bLobbyReady ? TEXT("READY ✓ · click to cancel") : TEXT("READY")); })]);
            Body->AddSlot().FillWidth(1).Padding(0,0,28,0)[Teams];
            Body->AddSlot().FillWidth(1.1f)[Actions];
            Add(Panel, Body);

        }
        else if (MenuPage == 1)
        {
            GI->SessionMode = 0;
            Field=Columns(Panel);
            Choice(Field(), TEXT("Map"), ERMaps::Labels(), [GI]() { return GI->MapChoice; }, [this, GI](int32 Index)
            {
                if (GI->MapChoice == Index) return;
                GI->MapChoice = Index;
                GI->OfflineCrossingChoice = GI->OfflineSkyChoice = 0;
                OpenMenuPage(1);
            });
            const auto& Map = ERMaps::At(GI->MapChoice);
            if (FCString::Strcmp(Map.Id, TEXT("central_building")) == 0)
                IntChoice(TEXT("Map variant"), {TEXT("Random"), TEXT("Open yard"), TEXT("Climbable wall"), TEXT("Fenced crossings")}, &GI->OfflineCrossingChoice);
            if (EROutdoorBackdrop::IsOutdoorMap(Map.Package))
            {
                struct FSkyPreview
                {
                    TStrongObjectPtr<UTexture2D> Texture;
                    FSlateBrush Brush;
                    FVector2D Aspect{1, 1};
                };
                const auto Preview = MakeShared<FSkyPreview>();
                const auto SelectSky = [GI, Preview](int32 Index)
                {
                    GI->OfflineSkyChoice = Index;
                    Preview->Texture.Reset(EROutdoorBackdrop::LoadOfflineSkyPreview(Index, Preview->Aspect));
                    Preview->Brush.SetResourceObject(Preview->Texture.Get());
                };
                SelectSky(GI->OfflineSkyChoice);
                const auto SkyField = Field();
                Choice(SkyField, TEXT("Skybox"), EROutdoorBackdrop::OfflineSkyLabels(),
                    [GI]() { return GI->OfflineSkyChoice; }, SelectSky);
                Add(SkyField, SNew(SBox).HAlign(HAlign_Center)
                    .Visibility_Lambda([Preview]() { return Preview->Texture.IsValid() ? EVisibility::Visible : EVisibility::Collapsed; })
                    [SNew(SBox).HeightOverride(132)
                        .WidthOverride_Lambda([Preview]() { return FOptionalSize(float(132 * Preview->Aspect.X)); })
                        [SNew(SImage).Image_Lambda([Preview]() { return &Preview->Brush; })]]);
            }
            TArray<FString> BotCounts; for (int32 I = 0; I <= 10; ++I) BotCounts.Add(FString::FromInt(I));
            IntChoice(TEXT("Alpha bots"), BotCounts, &GI->AlphaBots);
            IntChoice(TEXT("Omega bots"), BotCounts, &GI->OmegaBots);
            IntChoice(TEXT("Bot behavior"), {TEXT("Stationary targets"), TEXT("Movement only"), TEXT("Movement and combat")}, &GI->BotBehavior);
            Add(Panel, Button(TEXT("Player"), Go(6)));
            Add(Panel, Button(TEXT("Gameplay settings"),Go(10)));
            OfflineStart = Button(TEXT("Start Match"), [GI]() { GI->StartSession(); return FReply::Handled(); });
        }
        else if (MenuPage == 2)
        {
            Field=Columns(Panel);
            TArray<FString> BotCounts; for (int32 I = 0; I <= 10; ++I) BotCounts.Add(FString::FromInt(I));
            IntChoice(TEXT("Alpha bots (testing)"), BotCounts, &GI->AlphaBots);
            IntChoice(TEXT("Omega bots (testing)"), BotCounts, &GI->OmegaBots);
            auto Capacity=Field();
            Add(Capacity, Text(TEXT("Maximum human players (including you)")));
            Add(Capacity, SNew(SNumericEntryBox<int32>).Font(MenuFont(25)).MinValue(1)
                .MinSliderValue(1).MaxSliderValue(64).AllowSpin(true)
                .Value_Lambda([GI]() { return TOptional<int32>(GI->MaxPlayers); })
                .OnValueChanged_Lambda([GI](int32 Value) { GI->MaxPlayers = ERServer::ValidMaxPlayers(Value); })
                .OnValueCommitted_Lambda([GI](int32 Value, ETextCommit::Type) { GI->MaxPlayers = ERServer::ValidMaxPlayers(Value); GI->SaveServerSettings(); }));
            auto Sizing=Field();
            Add(Sizing,Text(TEXT("Expected players per team (map sizing only)")));
            Add(Sizing,SNew(SNumericEntryBox<int32>).Font(MenuFont(25)).MinValue(1).MaxValue(16).AllowSpin(true)
                .Value_Lambda([GI](){return TOptional<int32>(FMath::RoundToInt(GI->ServerRules.Get("Map.ExpectedTeamSize")));})
                .OnValueChanged_Lambda([GI](int32 N){GI->ServerRules.Set("Map.ExpectedTeamSize",N);})
                .OnValueCommitted_Lambda([GI](int32 N,ETextCommit::Type){GI->ServerRules.Set("Map.ExpectedTeamSize",N);GI->SaveServerSettings();}));
            Add(Sizing,Text(TEXT("750 m² per expected player (±10%). Fixed when the server is created; capacity is independent."),18));
            Choice(Field(), TEXT("Connection"), {TEXT("Internet (Epic Online Services)"), TEXT("LAN")},
                [GI]() { return GI->bInternetMenu ? 0 : 1; },
                [GI, Online](int32 I) { Online->Cancel(); GI->CancelLANSearch(); GI->bInternetMenu = I == 0; });
            auto ServerNameField = SNew(SVerticalBox)
                .Visibility_Lambda([GI]() { return GI->bInternetMenu ? EVisibility::Visible : EVisibility::Collapsed; });
            Add(ServerNameField, Text(TEXT("Server name")));
            Add(ServerNameField, SNew(SEditableTextBox).Font(MenuFont(26))
                .Text(FText::FromString(GI->InternetServerName))
                .OnTextChanged_Lambda([GI](const FText& Value) { GI->InternetServerName = Value.ToString().Left(48); })
                .OnTextCommitted_Lambda([GI](const FText&, ETextCommit::Type) { GI->SaveServerSettings(); }));
            Add(Panel, ServerNameField);
            Add(Panel, Text(TEXT("Internet: no account required. LAN: same local network. Test bots are additional."), 21));
            Add(Panel, SNew(STextBlock).Font(MenuFont(20)).AutoWrapText(true)
                .Visibility_Lambda([Online]() { return Online->Status.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
                .Text_Lambda([Online]() { return FText::FromString(Online->Status); }));
            Add(Panel, Button(TEXT("Gameplay settings"),Go(10)));
            Add(Panel, Button(TEXT("Start Server & Enter Lobby"), [GI, Online]() { if (GI->bInternetMenu) Online->Host(); else { Online->Cancel(); GI->StartLANServer(); } return FReply::Handled(); }));
        }
        else if(MenuPage==10)
        {
            auto Compact=[](const FString& Label,TFunction<FReply()> Action)->TSharedRef<SWidget>
            {auto LabelText=Text(Label,21);LabelText->SetAutoWrapText(false);return SNew(SButton).ContentPadding(FMargin(12,6)).OnClicked_Lambda(MoveTemp(Action))[LabelText];};
            auto RulesBack=Compact(TEXT("Back"),Back);Focus=RulesBack;Add(Panel,RulesBack);
            const bool Editable=Main && !Lobby;
            const FERServerRules* Rules=Editable?&GI->ServerRules:&ERServer::Rules(GetWorld());
            Add(Panel,Text(Editable?TEXT("Saved for your next hosted match. Modified values are marked *."):TEXT("Active server rules · read only"),21));
            Add(Panel,Text(Rules->IsStandard()?TEXT("Standard gameplay"):FString::Printf(TEXT("Custom gameplay · %d overrides"),Rules->Values.Num()),22));
            TArray<FString> Sections;
            for(const auto& S:ERServer::Settings())Sections.AddUnique(S.Section);
            Choice(Panel,TEXT("Category"),Sections,[this,Sections](){return FMath::Max(0,Sections.IndexOfByKey(ServerRulesSection));},
                [this,Sections](int32 I){ServerRulesSection=Sections[I];OpenMenuPage(10);});
            Add(Panel,SNew(SCheckBox).IsChecked(bServerRulesAdvanced?ECheckBoxState::Checked:ECheckBoxState::Unchecked)
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State){bServerRulesAdvanced=State==ECheckBoxState::Checked;OpenMenuPage(10);})[SNew(STextBlock).Font(MenuFont(22)).Text(FText::FromString(TEXT("Show advanced settings")))]);
            auto Management=SNew(SVerticalBox);
            const auto Fields=Columns(Panel);
            for(const auto& Definition:ERServer::Settings())
            {
                if(Definition.Section!=ServerRulesSection || (Definition.Advanced && !bServerRulesAdvanced))continue;
                const auto* S=&Definition;
                const float Base=ServerSettingDefault(*S,Catalog);
                auto Cell=Fields();
                Add(Cell,Text(S->Label+(Rules->Has(S->Key)?TEXT(" *"):TEXT(""))+(S->Unit.IsEmpty()?TEXT(""):TEXT(" · ")+S->Unit),22));
                auto Row=SNew(SHorizontalBox);
                if(S->Unit==TEXT("on/off"))
                    Row->AddSlot().FillWidth(1)[SNew(SCheckBox).IsEnabled(Editable)
                        .IsChecked_Lambda([Rules,S](){return Rules->Get(S->Key)>0?ECheckBoxState::Checked:ECheckBoxState::Unchecked;})
                        .OnCheckStateChanged_Lambda([this,GI,S,Editable](ECheckBoxState State){if(Editable){GI->ServerRules.Set(S->Key,State==ECheckBoxState::Checked?1.f:0.f);GI->SaveServerSettings();OpenMenuPage(10);}})
                        [Text(TEXT("Enabled"),22)]];
                else Row->AddSlot().FillWidth(1)[SNew(SNumericEntryBox<float>).Font(MenuFont(25)).MinValue(S->Min).MaxValue(S->Max)
                    .MinSliderValue(S->Min).MaxSliderValue(S->Max).AllowSpin(Editable).IsEnabled(Editable)
                    .MinFractionalDigits(0).MaxFractionalDigits(S->Integer?0:3)
                    .UndeterminedString(FText::FromString(TEXT("Standard (asset)")))
                    .Value_Lambda([Rules,S,Base](){return TOptional<float>(Rules->Get(S->Key,Base));})
                    .OnValueChanged_Lambda([GI,S,Editable](float V){if(Editable)GI->ServerRules.Set(S->Key,V);})
                    .OnValueCommitted_Lambda([this,GI,S,Editable](float V,ETextCommit::Type){if(Editable){GI->ServerRules.Set(S->Key,V);GI->SaveServerSettings();OpenMenuPage(10);}})];
                if(Editable)Row->AddSlot().AutoWidth().Padding(8,0,0,0)[Compact(TEXT("Standard"),[this,GI,S](){GI->ServerRules.Reset(S->Key);GI->SaveServerSettings();OpenMenuPage(10);return FReply::Handled();})];
                Add(Cell,Row);
                if(S->Key==FName("Claymore.TriggerPercent"))Add(Cell,SNew(STextBlock).Font(MenuFont(20)).Text_Lambda([Rules](){return FText::FromString(FString::Printf(TEXT("Effective trigger radius: %.2f m"),Rules->Get("Claymore.DamageRadius")*Rules->Get("Claymore.TriggerPercent")*.01f));}));
            }
            if(Editable)
            {
                auto Resets=SNew(SHorizontalBox);
                Resets->AddSlot().AutoWidth().Padding(0,0,12,0)[Compact(TEXT("Reset category"),[this,GI](){for(const auto& S:ERServer::Settings())if(S.Section==ServerRulesSection)GI->ServerRules.Reset(S.Key);GI->SaveServerSettings();OpenMenuPage(10);return FReply::Handled();})];
                Resets->AddSlot().AutoWidth()[Compact(TEXT("Reset all to Standard"),[this,GI](){GI->ServerRules.Values.Reset();GI->SaveServerSettings();OpenMenuPage(10);return FReply::Handled();})];
                Add(Management,Resets);
                Add(Management,SNew(SEditableTextBox).Font(MenuFont(24)).HintText(FText::FromString(TEXT("Preset name"))).Text(FText::FromString(ServerPresetName))
                    .OnTextChanged_Lambda([this](const FText& T){ServerPresetName=T.ToString().Left(32);}));
                TArray<FString> Presets;GConfig->GetArray(TEXT("RespawnAndRage.ServerPresets"),TEXT("Names"),Presets,GGameUserSettingsIni);
                auto PresetActions=SNew(SHorizontalBox);
                PresetActions->AddSlot().AutoWidth().Padding(0,0,12,0)[Compact(TEXT("Save preset"),[this,GI]()
                {
                    FString Name;for(TCHAR C:ServerPresetName)if(FChar::IsAlnum(C) || C==TCHAR(' ') || C==TCHAR('-') || C==TCHAR('_'))Name.AppendChar(C);
                    Name.TrimStartAndEndInline();if(Name.IsEmpty())return FReply::Handled();
                    ServerPresetName=Name;GI->SaveServerSettings();
                    ERServer::Save(GI->ServerRules,GGameUserSettingsIni,TEXT("RespawnAndRage.ServerPreset.")+Name);
                    TArray<FString> Names;GConfig->GetArray(TEXT("RespawnAndRage.ServerPresets"),TEXT("Names"),Names,GGameUserSettingsIni);Names.AddUnique(Name);
                    GConfig->SetArray(TEXT("RespawnAndRage.ServerPresets"),TEXT("Names"),Names,GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);OpenMenuPage(10);return FReply::Handled();
                })];
                Add(Management,PresetActions);
                if(!Presets.IsEmpty())Choice(Management,TEXT("Load saved preset"),Presets,
                    [this,Presets](){return FMath::Max(0,Presets.IndexOfByKey(ServerPresetName));},
                    [this,GI,Presets](int32 I){ServerPresetName=Presets[I];ERServer::Load(GI->ServerRules,GGameUserSettingsIni,TEXT("RespawnAndRage.ServerPreset.")+ServerPresetName);GI->SaveServerSettings();OpenMenuPage(10);});
            }
            if(Editable)Add(Panel,SNew(SExpandableArea).InitiallyCollapsed(true)
                .HeaderContent()[Text(TEXT("Presets and reset"),22)].BodyContent()[Management]);
        }
        else if (MenuPage == 3)
        {
            Choice(Panel, TEXT("Connection"), {TEXT("Internet (Epic Online Services)"), TEXT("LAN")},
                [GI]() { return GI->bInternetMenu ? 0 : 1; },
                [GI, Online](int32 I) { Online->Cancel(); GI->CancelLANSearch(); GI->bInternetMenu = I == 0; });
            Add(Panel, SNew(STextBlock).Font(MenuFont(28)).AutoWrapText(true).Text_Lambda([GI, Online]()
            { return FText::FromString(GI->bInternetMenu ? (Online->Status.IsEmpty() ? GI->LastError : Online->Status) : GI->IsSearchingLAN() ? FString::Printf(TEXT("Searching LAN... %ds"), GI->LANSecondsLeft()) : GI->LastError); }));
            Add(Panel, Text(TEXT("Internet: search for a server or connect directly. LAN: we connect automatically to an available local server."), 21));
            auto Servers = SNew(SVerticalBox);
            for (int32 I = 0; I < 100; ++I)
                Add(Servers, SNew(SButton).ContentPadding(8)
                    .Visibility_Lambda([GI, Online, I]() { return GI->bInternetMenu && Online->Results.IsValidIndex(I) ? EVisibility::Visible : EVisibility::Collapsed; })
                    .IsEnabled_Lambda([Online]() { return !Online->IsBusy(); })
                    .OnClicked_Lambda([Online, I]() { Online->Join(I); return FReply::Handled(); })
                    [SNew(STextBlock).Font(MenuFont(25)).AutoWrapText(true).Text_Lambda([Online, I]()
                    {
                        if (!Online->Results.IsValidIndex(I)) return FText::GetEmpty();
                        const auto& Session = Online->Results[I].Session;
                        return FText::FromString(UEROnline::BrowserLabel(Session.SessionSettings, Session.NumOpenPublicConnections));
                    })]);
            Add(Panel, SNew(SBox).MaxDesiredHeight(240)[SNew(SScrollBox) + SScrollBox::Slot()[Servers]]);
            Add(Panel, SNew(STextBlock).Font(MenuFont(21)).AutoWrapText(true)
                .Visibility_Lambda([GI, Online]()
                {
                    const bool bCommunity = GI->bInternetMenu && Online->Results.ContainsByPredicate([](const FOnlineSessionSearchResult& Result)
                        { return UEROnline::IsCommunityServer(Result.Session.SessionSettings); });
                    return bCommunity ? EVisibility::Visible : EVisibility::Collapsed;
                })
                .Text(FText::FromString(TEXT("[Community] servers are run by other players, not by Respawn & Rage. Join them at your own risk."))));
            Add(Panel, Button(TEXT("Search Servers"), [GI, Online]() { if (GI->bInternetMenu) Online->Search(); else { Online->Cancel(); GI->FindLANServer(); } return FReply::Handled(); }));
            auto Direct = SNew(SVerticalBox)
                .Visibility_Lambda([GI]() { return GI->bInternetMenu ? EVisibility::Visible : EVisibility::Collapsed; });
            auto JoinDirect = [GI, Online]()
            {
                Online->ConnectDirect(GI->DirectServerHost, GI->DirectServerPort, GI->bDirectServerUsesEOS);
                return FReply::Handled();
            };
            auto HostField = SNew(SVerticalBox);
            Add(HostField, Text(TEXT("Hostname or IP"), 21));
            Add(HostField, SNew(SEditableTextBox).Font(MenuFont(24))
                .HintText(FText::FromString(TEXT("server.example.com")))
                .Text(FText::FromString(GI->DirectServerHost))
                .OnTextChanged_Lambda([GI](const FText& Value) { GI->DirectServerHost = Value.ToString(); }));
            auto PortField = SNew(SVerticalBox);
            Add(PortField, Text(TEXT("Port"), 21));
            Add(PortField, SNew(SEditableTextBox).Font(MenuFont(24))
                .Text(FText::FromString(GI->DirectServerPort))
                .OnTextChanged_Lambda([GI](const FText& Value) { GI->DirectServerPort = Value.ToString(); }));
            Add(Direct, SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(3).Padding(0, 0, 12, 0)[HostField]
                + SHorizontalBox::Slot().FillWidth(1)[PortField]);
            Add(Direct, SNew(SCheckBox)
                .IsChecked_Lambda([GI]() { return GI->bDirectServerUsesEOS ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([GI](ECheckBoxState State) { GI->bDirectServerUsesEOS = State == ECheckBoxState::Checked; })
                [Text(TEXT("Use Epic Online Services sign-in"), 21)]);
            Add(Direct, SNew(SButton).ContentPadding(FMargin(18, 10))
                .IsEnabled_Lambda([Online]() { return !Online->IsBusy() && !Online->IsOnline(); })
                .OnClicked_Lambda(JoinDirect)[Text(TEXT("Connect Directly"), 25)]);
            Add(Panel, Direct);
            Add(Panel, Button(TEXT("Cancel"), Back));
        }
        else if (MenuPage == 4)
        {
            Add(Panel, Button(TEXT("Controls"), Go(11)));
            Add(Panel, Button(TEXT("Performance"), Go(8)));
            Field=Columns(Panel);
            auto Display=Field();
            auto Displays = SNew(SVerticalBox);
            for (int32 Percent : {50,60,70,80,90,100})
                Add(Displays, Button(Percent == 100 ? TEXT("Fullscreen") : FString::Printf(TEXT("Windowed %d%%"), Percent), [this, Percent]() { ApplyDisplaySize(Percent); return FReply::Handled(); }));
            Add(Display, Text(TEXT("Display")));
            Add(Display, SAssignNew(DisplaySizeSelector, SComboButton)
                .ButtonContent()[SNew(STextBlock).Font(MenuFont(25)).Text_Lambda([this]() { return GetDisplaySizeText(); })].MenuContent()[Displays]);
            auto Slider = [Field](const FString& Label, TFunction<float()> Get, TFunction<void(float)> Set, TFunction<void()> Save)
            {
                auto Cell=Field();
                Add(Cell, SNew(STextBlock).Font(MenuFont(24)).Text_Lambda([Label, Get]() { return FText::FromString(FString::Printf(TEXT("%s · %.0f%%"), *Label, Get() * 100)); }));
                Add(Cell, SNew(SBox).HeightOverride(32)[SNew(SSlider).Value_Lambda([Get]() { return Get(); }).StepSize(.01f)
                    .OnValueChanged_Lambda([Set, Save](float V) { Set(V); Save(); })]);
            };
            Slider(TEXT("Sound volume"), [GI]() { return GI->GetSoundVolume(); }, [GI](float V) { GI->SetSoundVolume(V); }, [GI]() { GI->SaveMusicVolume(); });
            Slider(TEXT("Music"), [GI]() { return GI->GetMusicVolume(); }, [GI](float V) { GI->SetMusicVolume(V); }, [GI]() { GI->SaveMusicVolume(); });
            Slider(TEXT("Mouse sensitivity"), [GI]() { return GI->GetMouseSensitivity(); }, [GI](float V) { GI->SetMouseSensitivity(V); }, [GI]() { GI->SaveMouseSensitivity(); });
            Slider(TEXT("Mouse acceleration"), [GI]() { return GI->GetMouseAcceleration(); }, [GI](float V) { GI->SetMouseAcceleration(V); }, [GI]() { GI->SaveMouseSensitivity(); });
            Add(Panel, Button(TEXT("Credits"), Go(7)));
        }
        else if (MenuPage == 11)
        {
            Add(Panel, Text(TEXT("Select a binding, then press a key or use the mouse over the button. Used keys swap. Changes save immediately."), 21));
            Add(Panel, Text(TEXT("Double-tap forward to sprint. Double-tap crouch to go prone."), 21));
            Add(Panel, Button(TEXT("Restore default controls"), [this, GI]()
            {
                GI->Controls.Reset();
                ApplyControlBindings();
                ControlBindingFeedback = FText::FromString(TEXT("Default controls restored."));
                return FReply::Handled();
            }));
            auto Column = Columns(Panel);
            for (const FERControlBinding& Item : FERControls::Bindings())
            {
                const FKey DefaultKey = Item.DefaultKey;
                Add(Column(), SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0, 0, 8, 0)[Text(Item.Label, 22)]
                    + SHorizontalBox::Slot().AutoWidth()
                    [SNew(SControlKeyButton)
                        .KeyText_Lambda([this, DefaultKey]() { return FText::FromString(FERControls::ShortLabel(ControlKey(DefaultKey))); })
                        .OnSelected([this, GI, DefaultKey](const FKey& NewKey)
                        {
                            if (GI->Controls.Rebind(DefaultKey, NewKey))
                            {
                                ApplyControlBindings();
                                ControlBindingFeedback = FText::GetEmpty();
                            }
                            else ControlBindingFeedback = FText::FromString(TEXT("Choose a keyboard key or mouse button. Wheel scrolling is reserved for cycling items."));
                        })]);
            }
        }
        else if (MenuPage == 8)
        {
            auto* Settings=&ERPerformance::Get();
            auto Apply=[this,Settings]() { Settings->Defaults=0; Settings->Apply(); Settings->Save(); ResetRenderResolution(); };
            auto Action=Columns(Panel);
            auto Preset=[this,Settings,Action](const TCHAR* Label,ERPerformance::EPreset Choice)
            { Add(Action(), Button(Label, [this,Settings,Choice]() { Settings->ApplyPreset(Choice); ResetRenderResolution(); OpenMenuPage(8); return FReply::Handled(); })); };
            Preset(TEXT("Competitive preset · max FPS"),ERPerformance::EPreset::Competitive);
            Preset(TEXT("Performance preset"),ERPerformance::EPreset::Performance);
            Preset(TEXT("Balanced preset"),ERPerformance::EPreset::Balanced);
            Preset(TEXT("Quality preset"),ERPerformance::EPreset::Quality);
            Add(Action(), Button(TEXT("Set default"), [this,Settings]() { Settings->SetDefaults(); ResetRenderResolution(); OpenMenuPage(8); return FReply::Handled(); }));
            Add(Panel,Text(TEXT("Default restores the original project graphics and automatic resolution."),20));
            Field=Columns(Panel);
            auto Option=[Field,Apply](const FString& Label,TArray<FString> Labels,int32* Value)
            { Choice(Field(),Label,Labels,[Value]() { return *Value; },[Value,Apply](int32 I) { *Value=I; Apply(); }); };
            Option(TEXT("Internal resolution"),{TEXT("Manual"),TEXT("Automatic · 60 FPS / max 1080p")},&Settings->Automatic);
            auto Scale=Field();
            Add(Scale,SNew(STextBlock).Font(MenuFont(24)).Text_Lambda([Settings]() { return FText::FromString(FString::Printf(TEXT("Manual internal scale · %d%%"),Settings->Scale)); }));
            Add(Scale,SNew(SSlider).MinValue(1).MaxValue(100).StepSize(1)
                .IsEnabled_Lambda([Settings]() { return !Settings->Automatic; })
                .Value_Lambda([Settings]() { return float(Settings->Scale); })
                .OnValueChanged_Lambda([Settings,Apply](float V) { Settings->Scale=FMath::Clamp(FMath::RoundToInt(V),1,100); Apply(); }));
            Add(Scale,Text(TEXT("Manual scale is relative to the window; HUD stays sharp. Very low scales lose detail."),20));
            Option(TEXT("Antialiasing / upscaling"),{TEXT("TSR"),TEXT("TAA · spatial upscale"),TEXT("TAAU · temporal upscale")},&Settings->AA);
            Option(TEXT("Global illumination"),{TEXT("Off · no Lumen"),TEXT("Lumen")},&Settings->GI);
            Option(TEXT("Lumen quality"),{TEXT("Medium"),TEXT("High")},&Settings->LumenQuality);
            Option(TEXT("Hardware ray tracing (supported GPUs only)"),{TEXT("Off"),TEXT("Enabled when supported")},&Settings->HardwareRT);
            Option(TEXT("Shadows · resolution and distance"),{TEXT("Off"),TEXT("Low"),TEXT("Medium"),TEXT("High")},&Settings->Shadows);
            Option(TEXT("Reflections"),{TEXT("Off"),TEXT("Screen space · simple"),TEXT("Lumen")},&Settings->Reflections);
            Option(TEXT("Motion blur"),{TEXT("Off"),TEXT("On")},&Settings->MotionBlur);
            Option(TEXT("Depth of field"),{TEXT("Off"),TEXT("On")},&Settings->DepthOfField);
            Option(TEXT("Decorative effects"),{TEXT("Minimum"),TEXT("Low"),TEXT("Medium"),TEXT("High")},&Settings->Effects);
            Option(TEXT("Post processing · AO and bloom"),{TEXT("Low"),TEXT("Medium"),TEXT("High"),TEXT("Ultra")},&Settings->PostProcess);
            Option(TEXT("Frame rate limit"),{TEXT("60"),TEXT("90"),TEXT("120"),TEXT("144"),TEXT("Unlimited")},&Settings->FrameCap);
            Option(TEXT("Textures · choose according to GPU memory"),{TEXT("Low"),TEXT("Medium"),TEXT("High"),TEXT("Ultra")},&Settings->Textures);
            Add(Panel,Text(TEXT("Changes apply immediately and are saved. Texture streaming uses the hardware memory budget. Gameplay smoke remains unchanged."),20));
            Add(Panel,Button(TEXT("Reset Automatic Resolution"),[this]() { ResetRenderResolution(); return FReply::Handled(); }));
        }
        else if (MenuPage == 9)
        {
            Add(Panel,Text(TEXT("Suicide now, or join the selected team on your next respawn?")));
            const auto Confirm=[this,GI](bool Suicide)
            {
                GI->PlayerTeam=RequestedTeam;GI->SaveProfile();
                ServerChangeTeam(RequestedTeam,Suicide);OpenMenuPage(6);return FReply::Handled();
            };
            Add(Panel,Button(TEXT("Suicide and change team"),[Confirm]() { return Confirm(true); }));
            Add(Panel,Button(TEXT("Wait for next respawn"),[Confirm]() { return Confirm(false); }));
            Add(Panel,Button(TEXT("Cancel"),Go(6)));
        }
        else if (MenuPage == 6)
        {
            auto Identity=SNew(SHorizontalBox);
            auto NameField=SNew(SVerticalBox);auto TeamField=SNew(SVerticalBox);
            Identity->AddSlot().FillWidth(1).Padding(0,0,20,0)[NameField];
            Identity->AddSlot().FillWidth(1)[TeamField];
            Add(Panel,Identity);
            Add(NameField,Text(TEXT("Player name"),22));
            Add(NameField,SAssignNew(NameBox,SEditableTextBox).Font(MenuFont(26)).Text(FText::FromString(GI->LocalName))
                .OnTextCommitted_Lambda([this,GI](const FText& Value,ETextCommit::Type) { GI->LocalName=Value.ToString();GI->SaveProfile();ServerSetName(GI->LocalName); }));
            Choice(TeamField,TEXT("Team"),{TEXT("Alpha"),TEXT("Omega")},[this,GI]()
            {
                const auto* PS=GetPlayerState<AERPlayerState>();
                return (!IsMenuLevel() || IsInLobby()) && PS ? int32((PS->PendingTeam!=EERTeam::None?PS->PendingTeam:PS->Team)==EERTeam::Omega):GI->PlayerTeam;
            },[this,GI](int32 I)
            {
                const auto* PS=GetPlayerState<AERPlayerState>();
                const auto* C=Cast<AERCharacter>(GetPawn());
                if(!IsMenuLevel() && PS && C && !C->Health->bDead && I!=int32(PS->Team==EERTeam::Omega))
                { RequestedTeam=I;OpenMenuPage(9);return; }
                GI->PlayerTeam=I;GI->SaveProfile();
                if(IsInLobby())ServerLobbyTeam(I);else if(!IsMenuLevel())ServerChangeTeam(I,false);
                RefreshLoadoutPreview();
            });
            Add(Panel,SNew(STextBlock).Font(MenuFont(18)).Visibility_Lambda([this]()
            {
                const auto* PS=GetPlayerState<AERPlayerState>();
                return PS && PS->PendingTeam!=EERTeam::None ? EVisibility::Visible:EVisibility::Collapsed;
            }).Text_Lambda([this]()
            {
                const auto* PS=GetPlayerState<AERPlayerState>();
                return FText::FromString(PS && PS->PendingTeam!=EERTeam::None ? FString(TEXT("Team at next respawn: "))+(PS->PendingTeam==EERTeam::Omega?TEXT("Omega"):TEXT("Alpha")):FString());
            }));
            if(DraftPreset.IsNone()) { DraftPreset=GI->LoadoutPreset;DraftVariant=GI->LoadoutVariant; }
            const auto* PS=(!Main || Lobby)?GetPlayerState<AERPlayerState>():nullptr;
            const bool Omega=PS && PS->Team!=EERTeam::None ? (PS->PendingTeam!=EERTeam::None?PS->PendingTeam:PS->Team)==EERTeam::Omega:GI->PlayerTeam==1;
            auto Body=SNew(SHorizontalBox);auto Roles=SNew(SVerticalBox);auto Variants=SNew(SVerticalBox);
            if(Catalog && !MenuPreview)MenuPreview=GetWorld()->SpawnActor<AERMenuPreview>(FVector(0,0,-100000),FRotator::ZeroRotator);
            if(LoadoutIconBrushes.IsEmpty())
            {
                const TCHAR* Names[]={TEXT("01_knife"),TEXT("02_pistol"),TEXT("03_smg_mp9"),TEXT("04_shotgun_xm1014"),TEXT("05_assault_rifle"),TEXT("06_carbine_marksman"),TEXT("07_sniper_rifle"),TEXT("08_grenade_launcher_m32"),TEXT("09_he_grenade"),TEXT("10_smoke_grenade"),TEXT("11_claymore")};
                const FIntRect Bounds[]={{131,159,893,353},{216,86,808,425},{121,93,903,418},{58,152,965,359},{61,108,963,403},{66,116,957,396},{41,132,983,379},{76,121,947,391},{357,72,667,439},{420,72,603,439},{436,100,1338,806}};
                for(int32 I=0;I<11;++I)
                {
                    auto* Texture=LoadObject<UTexture2D>(nullptr,*FString::Printf(TEXT("/Game/EclipseRecoil/Art/HUDWeapons/T_%s.T_%s"),Names[I],Names[I]));
                    LoadoutIconTextures.Add(Texture);
                    auto Brush=MakeShared<FSlateBrush>();Brush->SetResourceObject(Texture);
                    const auto B=Bounds[I];Brush->ImageSize=FVector2D(B.Width(),B.Height());
                    if(Texture)Brush->SetUVRegion(FBox2f(FVector2f(float(B.Min.X)/Texture->GetSizeX(),float(B.Min.Y)/Texture->GetSizeY()),FVector2f(float(B.Max.X)/Texture->GetSizeX(),float(B.Max.Y)/Texture->GetSizeY())));
                    LoadoutIconBrushes.Add(Brush);
                }
            }
            Add(Roles,Text(TEXT("Role"),23));
            for(const FName Preset:ERLoadouts::Presets())
            {
                const auto Kit=ResolveMenuLoadout(Preset,DraftVariant);
                auto Card=SNew(SHorizontalBox);
                Card->AddSlot().AutoWidth().Padding(0,0,12,0)
                    [SNew(SBox).WidthOverride(104).HeightOverride(36)
                        [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SImage).Image(LoadoutIconBrushes[Kit.Items[0].WeaponKey].Get())]]];
                Card->AddSlot().FillWidth(1).VAlign(VAlign_Center)[Text(Kit.Role,23)];
                auto RoleBorder=SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor_Lambda([this,Preset]() { return DraftPreset==Preset?FLinearColor(.9f,.55f,.15f):FLinearColor(.12f,.14f,.17f); }).Padding(2)
                    [SNew(SBorder).BorderBackgroundColor(FLinearColor(.06f,.07f,.09f)).Padding(4)[Card]];
                Add(Roles,SNew(SButton).ContentPadding(4).OnClicked_Lambda([this,Preset]() { DraftPreset=Preset;RefreshLoadoutPreview();return FReply::Handled(); })[RoleBorder]);
            }
            Body->AddSlot().FillWidth(1).Padding(0,0,14,0)[Roles];
            if(MenuPreview)
            {
                RefreshLoadoutPreview();
                PreviewBrush=MakeShared<FSlateBrush>();PreviewBrush->SetResourceObject(MenuPreview->Target);PreviewBrush->ImageSize=FVector2D(768,1024);
                PlayerPreview=SNew(SPlayerPreview).Preview(MenuPreview).Cursor(EMouseCursor::GrabHand)
                    [SNew(SOverlay)
                    + SOverlay::Slot()[SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SImage).Image(PreviewBrush.Get())]]
                    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
                        [SNew(STextBlock).Font(MenuFont(24)).Text(FText::FromString(TEXT("Preview unavailable")))
                            .Visibility_Lambda([this]() { return MenuPreview && MenuPreview->bPreviewAvailable?EVisibility::Collapsed:EVisibility::Visible; })]];
            }
            else PlayerPreview=Text(TEXT("Preview unavailable"));
            Add(Variants,Text(TEXT("Variant"),23));
            auto VariantButtons=SNew(SHorizontalBox);
            for(const FName Variant:ERLoadouts::Variants())
            {
                const FString Name=LoadoutVariantName(Variant);
                VariantButtons->AddSlot().FillWidth(1).Padding(0,0,4,0)
                    [SNew(SButton).ContentPadding(FMargin(6,8)).OnClicked_Lambda([this,Variant]() { DraftVariant=Variant;RefreshLoadoutPreview();return FReply::Handled(); })
                        [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                            .BorderBackgroundColor_Lambda([this,Variant]() {return DraftVariant==Variant?FLinearColor(.35f,.22f,.07f):FLinearColor(.06f,.07f,.09f);})
                            .Padding(4)[Text(Name,20)]]];
            }
            Add(Variants,VariantButtons);
            Add(Variants,Text(TEXT("Equipment · magazine / total"),20));
            for(int32 Slot=0;Slot<5;++Slot)
            {
                auto Row=SNew(SHorizontalBox);
                Row->AddSlot().AutoWidth().Padding(0,0,12,0).VAlign(VAlign_Center)
                    [SNew(SBox).WidthOverride(76).HeightOverride(30)
                        [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)[SNew(SImage).Image_Lambda([this,Slot]() -> const FSlateBrush*
                        {
                            const auto Kit=ResolveMenuLoadout(DraftPreset,DraftVariant);
                            return Kit.Items.IsValidIndex(Slot)?LoadoutIconBrushes[Kit.Items[Slot].WeaponKey].Get():nullptr;
                        })]]];
                Row->AddSlot().FillWidth(1).VAlign(VAlign_Center)
                    [SNew(STextBlock).Font(MenuFont(18)).AutoWrapText(true).Text_Lambda([this,Slot]()
                    {
                        const auto Kit=ResolveMenuLoadout(DraftPreset,DraftVariant);
                        if(!Kit.Items.IsValidIndex(Slot))return FText::GetEmpty();
                        const auto& Item=Kit.Items[Slot];
                        FString Label=ERLoadouts::WeaponName(Item.WeaponKey);
                        if(Item.WeaponKey>=8)Label+=FString::Printf(TEXT("  x%d"),Item.Loaded+Item.Reserve);
                        else if(Item.WeaponKey!=0)Label+=FString::Printf(TEXT("  %d / %d"),Item.Loaded,Item.Loaded+Item.Reserve);
                        return FText::FromString(Label);
                    })];
                Variants->AddSlot().AutoHeight().Padding(0,3)
                    [SNew(SBox).Visibility_Lambda([this,Slot]() {return ResolveMenuLoadout(DraftPreset,DraftVariant).Items.IsValidIndex(Slot)?EVisibility::Visible:EVisibility::Collapsed;})[Row]];
            }
            Add(Variants,SNew(STextBlock).Font(MenuFont(17)).Text_Lambda([this](){const auto K=ResolveMenuLoadout(DraftPreset,DraftVariant);return FText::FromString(FString::Printf(TEXT("%.0f HP · %.0f armor%s"),K.StartingHealth,K.StartingArmor,K.Helmet?TEXT(" · Helmet"):TEXT("")));}));
            Body->AddSlot().FillWidth(1.2f).Padding(20,0,0,0)[Variants];Add(Panel,Body);
            Add(PlayerFooter,SNew(STextBlock).Font(MenuFont(19)).AutoWrapText(true).Text_Lambda([this,GI]()
            {
                const auto* State=(!IsMenuLevel() || IsInLobby())?GetPlayerState<AERPlayerState>():nullptr;const auto* C=Cast<AERCharacter>(GetPawn());
                const auto Next=ResolveMenuLoadout(State?State->LoadoutPreset:GI->LoadoutPreset,State?State->LoadoutVariant:GI->LoadoutVariant);
                FString Status=TEXT("Confirmed for next spawn: ")+Next.Role+TEXT(" / ")+LoadoutVariantName(Next.Variant);
                if(DraftPreset!=Next.Preset || DraftVariant!=Next.Variant)
                    Status=TEXT("Not applied. Next spawn is still ")+Next.Role+TEXT(" / ")+LoadoutVariantName(Next.Variant);
                if(C && !C->Health->bDead && (C->ActivePreset!=Next.Preset || C->ActiveVariant!=Next.Variant))
                {
                    const auto Current=ResolveMenuLoadout(C->ActivePreset,C->ActiveVariant);
                    Status=TEXT("Equipped: ")+Current.Role+TEXT(" / ")+LoadoutVariantName(Current.Variant)+TEXT("  ·  ")+Status;
                }
                if(bLoadoutPending)Status+=TEXT(" · Waiting for server");
                return FText::FromString(Status);
            }));
            Add(PlayerFooter,Text(TEXT("Changes take effect on your next spawn."),18));
            auto Actions=SNew(SHorizontalBox);
            Actions->AddSlot().AutoWidth()[Focus.ToSharedRef()];
            Actions->AddSlot().FillWidth(1).Padding(12,0,0,0)[Button(Main && !Lobby?TEXT("Save loadout"):TEXT("Apply for next spawn"),[this,GI,Main,Lobby]()
            {
                if(Main && !Lobby) { const auto Kit=ERLoadouts::Resolve(DraftPreset,DraftVariant);GI->LoadoutPreset=Kit.Preset;GI->LoadoutVariant=Kit.Variant;GI->SaveLoadout(); }
                else { bLoadoutPending=true;ServerSetLoadout(DraftPreset,DraftVariant); }
                return FReply::Handled();
            })];
            Add(PlayerFooter,Actions);
        }
        else if (MenuPage == 7)
        {
            FString Credits;
            if (FFileHelper::LoadFileToString(Credits, *(FPaths::ProjectDir() / TEXT("Branding/Credits.txt"))))
            {
                Credits.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
                TArray<FString> Sections;
                Credits.ParseIntoArray(Sections, TEXT("\n\n"), true);
                for (const FString& Section : Sections)
                {
                    FString Title, Body;
                    Section.Split(TEXT("\n"), &Title, &Body);
                    auto Heading = Text(Title, 30);
                    Heading->SetColorAndOpacity(FLinearColor(1.f, .68f, .22f));
                    Panel->AddSlot().AutoHeight().Padding(0, 18, 0, 5)[Heading];
                    Add(Panel, Text(Body.TrimEnd(), 24));
                }
            }
            else
            {
                Add(Panel, Text(TEXT("Credits could not be loaded.")));
                UE_LOG(LogTemp, Warning, TEXT("Missing Branding/Credits.txt"));
            }
        }
    }
    static const FSlateDynamicImageBrush Background(FName(*FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Branding/background.jpg"))), FVector2D(2560,1441));
    auto Overlay = SNew(SOverlay);
    if (Main) Overlay->AddSlot()[SNew(SScaleBox).Stretch(EStretch::ScaleToFill).Clipping(EWidgetClipping::ClipToBounds).Visibility(EVisibility::HitTestInvisible)[SNew(SImage).Image(&Background)]];
    if (Home)
        Overlay->AddSlot().HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(24,30,70,30)[SNew(SBox).WidthOverride(360)[Panel]];
    else
    {
        TSharedRef<SWidget> Content = SNew(SScrollBox) + SScrollBox::Slot()[Panel];
        if (OfflineStart.IsValid())
            Content = SNew(SVerticalBox)
                + SVerticalBox::Slot().FillHeight(1)[Content]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)[OfflineStart.ToSharedRef()];
        if(MenuPage==6)
            Content=SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
                [SNew(SBox).WidthOverride(1600).HeightOverride(900)
                    [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(.57f).Padding(0,0,28,0)
                            [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                                .BorderBackgroundColor(FLinearColor(.025f,.03f,.04f,.9f)).Padding(24)
                                [SNew(SVerticalBox)
                                    + SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)+SScrollBox::Slot()[Panel]]
                                    + SVerticalBox::Slot().AutoHeight().Padding(0,12,0,0)[PlayerFooter]]]
                        + SHorizontalBox::Slot().FillWidth(.43f)[PlayerPreview.ToSharedRef()]]];
        if (MenuPage == 7)
            Content = SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[Text(TEXT("CREDITS"), 36)]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)[Focus.ToSharedRef()]
                + SVerticalBox::Slot().FillHeight(1)[Content];
        if(MenuPage==6)
            Overlay->AddSlot().HAlign(HAlign_Fill).VAlign(VAlign_Fill).Padding(40,40,40,65)[Content];
        else Overlay->AddSlot().HAlign(HAlign_Fill).VAlign(VAlign_Center).Padding(70,50)
            [SNew(SBox).MaxDesiredHeight(850)
                [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.025f,.03f,.04f,.94f)).ForegroundColor(FLinearColor::White).Padding(26)
                    [Content]]];
    }
    if (MenuPage != 3) Overlay->AddSlot().VAlign(VAlign_Bottom).Padding(30,8,200,10)[SNew(STextBlock).Font(MenuFont(22)).ColorAndOpacity(FLinearColor(1,.65f,.3f)).ShadowOffset(FVector2D(2,2)).ShadowColorAndOpacity(FLinearColor::Black).AutoWrapText(true).Text_Lambda([this, GI]() { return MenuPage == 11 ? ControlBindingFeedback : FText::FromString(GI->LastError); })];
    auto Version=Text(GI->BuildVersion,16);
    Version->SetAutoWrapText(false);
    Overlay->AddSlot()
        [SNew(SSafeZone).IsTitleSafe(true).HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(32)
            .Visibility(EVisibility::HitTestInvisible)[Version]];
    MenuWidget = Overlay;
    GetWorld()->GetGameViewport()->AddViewportWidgetContent(MenuWidget.ToSharedRef(), 20);
    MenuFocusWidget = Focus;
    ApplyWindowInputMode();
}

void AERPlayerController::RefreshLoadoutPreview()
{
    if(!MenuPreview || !SkinCatalog)return;
    const auto* Catalog=SkinCatalog->GetDefaultObject<AERCharacter>();
    const auto* PS=(!IsMenuLevel() || IsInLobby())?GetPlayerState<AERPlayerState>():nullptr;const auto* GI=GetGameInstance<UERGameInstance>();
    const bool Omega=PS && PS->Team!=EERTeam::None?(PS->PendingTeam!=EERTeam::None?PS->PendingTeam:PS->Team)==EERTeam::Omega:GI->PlayerTeam==1;
    auto Kit=ResolveMenuLoadout(DraftPreset,DraftVariant);
    MenuPreview->ShowLoadout(ERSkins::Select(Omega?Catalog->OmegaSkins:Catalog->AlphaSkins,Omega?Catalog->OmegaAppearance:Catalog->AlphaAppearance,Kit.Skin),Kit,Catalog->ReferenceArsenal);
}

FERResolvedLoadout AERPlayerController::ResolveMenuLoadout(FName Preset,FName Variant) const
{
    const auto* GI=GetGameInstance<UERGameInstance>();
    const auto* PS=(!IsMenuLevel() || IsInLobby())?GetPlayerState<AERPlayerState>():nullptr;
    const bool Omega=PS && PS->Team!=EERTeam::None?(PS->PendingTeam!=EERTeam::None?PS->PendingTeam:PS->Team)==EERTeam::Omega:GI->PlayerTeam==1;
    const auto* Catalog=SkinCatalog?SkinCatalog->GetDefaultObject<AERCharacter>():nullptr;
    const auto Local=ERLoadouts::Resolve(Preset,Variant,Omega,Catalog,IsMenuLevel() && !IsInLobby()?&GI->ServerRules:&ERServer::Rules(GetWorld()));
    if(!IsMenuLevel() || IsInLobby())if(const auto* State=GetWorld()->GetGameState<AERGameState>())
        if(const auto* Server=State->LoadoutCatalog.FindByPredicate([&](const auto& Kit) { return Kit.Preset==Local.Preset && Kit.Variant==Local.Variant && Kit.bOmega==Omega; }))return *Server;
    return Local;
}
