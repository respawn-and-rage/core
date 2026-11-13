#include "ERPlayerController.h"
#include "ERMatch.h"
#include "ERCharacter.h"
#include "ERServerSettings.h"
#include "Components/CapsuleComponent.h"
#include "ERRecon.h"
#include "ERHUDLayout.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "CanvasItem.h"

namespace
{
    constexpr double KillFeedSeconds = 8.0;
    constexpr double KillFeedFade = .6;
    // Same crop on both supplied textures, preserving the transparent outline.
    const FVector2D BadgeUV0(206.0 / 1254.0, 163.0 / 1254.0);
    const FVector2D BadgeUV1(1031.0 / 1254.0, 1113.0 / 1254.0);
    constexpr float BadgeAspect = 825.f / 950.f;

    FString ShortName(const FString& Name, int32 Length = 18)
    {
        return Name.Len() > Length ? Name.Left(Length - 1) + TEXT("…") : Name;
    }

    FLinearColor NameColor(EERTeam Team)
    {
        return Team == EERTeam::None ? FLinearColor(.85f,.87f,.9f) : ERRecon::TeamColor(Team);
    }
}

void AERHUD::DrawPreMatchCountdown(const AERGameState* GS)
{
    const FERHUDLayout Layout(FVector2D(Canvas->SizeX,Canvas->SizeY));
    const float S = Layout.Scale;
    const int32 Seconds = FMath::Max(0, FMath::CeilToInt32(GS->CountdownEndServerTime - GS->GetServerWorldTimeSeconds()));
    const float CX = Layout.Position(640, 0).X;
    const FLinearColor Caption(.82f,.85f,.9f);

    // Stack from the bottom up using font measurements, leaving the reticle clear.
    const FString Title = TEXT("MATCH STARTS IN");
    const FString Count = Seconds > 0 ? FString::FromInt(Seconds) : TEXT("GO");
    const FString Hint = TEXT("Movement and weapons unlock at match start");
    constexpr float TitleScale = .75f, CountScale = 1.9f, HintScale = .68f;
    const FVector2D TitleSize = MeasureHUDText(Title, TitleScale);
    const FVector2D CountSize = MeasureHUDText(Count, CountScale);
    const FVector2D HintSize = MeasureHUDText(Hint, HintScale);
    const float HintY = Layout.Position(640, 292).Y - HintSize.Y;
    const float CountY = HintY - CountSize.Y - 3.f * S;
    const float TitleY = CountY - TitleSize.Y - 3.f * S;
    DrawHUDText(Title, CX, TitleY, Caption, TitleScale, true);
    DrawHUDText(Count, CX, CountY, Seconds <= 3 ? FLinearColor(1.f,.55f,.25f) : FLinearColor(1.f,.82f,.42f), CountScale, true);
    DrawHUDText(Hint, CX, HintY, Caption, HintScale, true);

    const auto* PC = Cast<AERPlayerController>(PlayerOwner);
    const auto Bound = [PC](const FKey& DefaultKey) { return PC ? PC->ControlKey(DefaultKey) : DefaultKey; };
    const auto Label = [&](const FKey& DefaultKey) { return FERControls::ShortLabel(Bound(DefaultKey)); };
    const FString Forward = Label(EKeys::W), Backward = Label(EKeys::S);
    const FString Crouch = Label(EKeys::C) + TEXT(" / ") + Label(EKeys::LeftControl);
    const FString Jump = Label(EKeys::SpaceBar), Fire = Label(EKeys::LeftMouseButton);
    const FString Walk = Bound(EKeys::LeftShift) == EKeys::LeftShift && Bound(EKeys::RightShift) == EKeys::RightShift
        ? TEXT("SHIFT") : Label(EKeys::LeftShift) + TEXT(" / ") + Label(EKeys::RightShift);
    const FString Cycle = Bound(EKeys::MouseScrollUp) == EKeys::MouseScrollUp && Bound(EKeys::MouseScrollDown) == EKeys::MouseScrollDown
        ? TEXT("WHEEL") : Label(EKeys::MouseScrollUp) + TEXT(" / ") + Label(EKeys::MouseScrollDown);
    TArray<FString> SlotLabels;
    bool bDefaultNumberKeys = true;
    for (int32 Index = 0; Index < 8; ++Index)
        bDefaultNumberKeys &= Bound(FERControls::SlotKey(Index)) == FERControls::SlotKey(Index);
    if (bDefaultNumberKeys) SlotLabels.Add(TEXT("1-8"));
    for (int32 Index = bDefaultNumberKeys ? 8 : 0; Index < 11; ++Index)
        SlotLabels.Add(Label(FERControls::SlotKey(Index)));
    struct FControl { FString Keys; const TCHAR* Action = nullptr; };
    const FControl Rows[][5] = {
        {{Forward + TEXT(" ") + Label(EKeys::A) + TEXT(" ") + Backward + TEXT(" ") + Label(EKeys::D), TEXT("MOVE")},
         {TEXT("MOUSE"), TEXT("LOOK")}, {Walk, TEXT("WALK")},
         {Label(EKeys::E) + TEXT(" + ") + Forward + TEXT(" / ") + Forward + TEXT(" x2"), TEXT("SPRINT")}},
        {{Jump, TEXT("JUMP / CLIMB")}, {Forward + TEXT(" / ") + Backward, TEXT("USE LADDER")},
         {Crouch, TEXT("CROUCH")}, {Crouch, TEXT("DOUBLE-TAP TO GO PRONE")}},
        {{Fire, TEXT("FIRE / USE ITEM")}, {Label(EKeys::RightMouseButton), TEXT("AIM / ZOOM")},
         {Label(EKeys::R), TEXT("RELOAD")}, {Fire + TEXT(" / ") + Jump, TEXT("RESPAWN")}},
        {{FString::Join(SlotLabels, TEXT(" / ")), TEXT("SELECT ITEM (IF CARRIED)")}, {Cycle, TEXT("CYCLE ITEMS")}},
        {{Label(EKeys::Tab), TEXT("SCOREBOARD")}, {Label(EKeys::Escape), TEXT("MENU")}, {Label(EKeys::P), TEXT("SCREENSHOTS")}, {Label(EKeys::X), TEXT("FLASHLIGHT")}},
    };
    constexpr float KeyScale = .78f, ActionScale = .78f;
    const float KeyPad = 6.f * S, LabelGap = 7.f * S, GroupGap = 18.f * S;
    // The Teko line box includes generous leading. Fit the keycap around the
    // visible uppercase glyphs so all five rows clear the corner vitals.
    const float KeyHeight = .67f * MeasureHUDText(TEXT("M"), KeyScale).Y + 3.f * S;
    const float RowStep = KeyHeight + 4.f * S;
    const float FirstRowY = Layout.Position(640, 438).Y + 7.f * S;
    const FLinearColor RowBackdrop(0.f, 0.f, 0.f, .24f);
    const float BackdropPadX = 5.f * S, BackdropPadY = 1.5f * S;
    const FLinearColor KeyOutline(.78f,.8f,.83f,.85f);
    for (int32 Row = 0; Row < UE_ARRAY_COUNT(Rows); ++Row)
    {
        float KeyWidths[UE_ARRAY_COUNT(Rows[0])] = {};
        float ActionWidths[UE_ARRAY_COUNT(Rows[0])] = {};
        float RowWidth = 0.f;
        const auto MeasureRow = [&](float Fit)
        {
            RowWidth = 0.f;
            for (int32 I = 0; I < UE_ARRAY_COUNT(Rows[Row]) && Rows[Row][I].Action; ++I)
            {
                KeyWidths[I] = MeasureHUDText(Rows[Row][I].Keys, KeyScale * Fit).X + 2.f * KeyPad;
                ActionWidths[I] = MeasureHUDText(Rows[Row][I].Action, ActionScale * Fit).X;
                RowWidth += (I == 0 ? 0.f : GroupGap) + KeyWidths[I] + LabelGap + ActionWidths[I];
            }
        };
        float Fit = 1.f;
        MeasureRow(Fit);
        const float MaxRowWidth = Layout.Position(960, 0).X - 2.f * BackdropPadX;
        while (RowWidth > MaxRowWidth && Fit > .2f)
        {
            Fit *= .9f;
            MeasureRow(Fit);
        }
        float X = CX - RowWidth * .5f;
        const float Y = FirstRowY + Row * RowStep;
        DrawRect(RowBackdrop, X - BackdropPadX, Y - BackdropPadY,
            RowWidth + 2.f * BackdropPadX, KeyHeight + 2.f * BackdropPadY);
        for (int32 I = 0; I < UE_ARRAY_COUNT(Rows[Row]) && Rows[Row][I].Action; ++I)
        {
            const float KeyWidth = KeyWidths[I];
            DrawRect(KeyOutline, X, Y, KeyWidth, S);
            DrawRect(KeyOutline, X, Y + KeyHeight - S, KeyWidth, S);
            DrawRect(KeyOutline, X, Y + S, S, KeyHeight - 2.f * S);
            DrawRect(KeyOutline, X + KeyWidth - S, Y + S, S, KeyHeight - 2.f * S);
            const float Mid = Y + KeyHeight * .5f;
            DrawHUDTextAnchored(Rows[Row][I].Keys, FVector2D(X + KeyWidth * .5f, Mid), FLinearColor::White, KeyScale * Fit, FVector2D(.5f,.447f));
            X += KeyWidth + LabelGap;
            DrawHUDTextAnchored(Rows[Row][I].Action, FVector2D(X, Mid), Caption, ActionScale * Fit, FVector2D(0,.447f));
            X += ActionWidths[I] + GroupGap;
        }
    }
}

void AERHUD::DrawKillBadge(bool bHeadshot, FVector2D Position, float Height, float Opacity)
{
    const UTexture2D* Texture = bHeadshot ? HeadshotBadge.Get() : SkullBadge.Get();
    if (!Texture || !Texture->GetResource()) return;
    const FVector2D Size(Height * BadgeAspect, Height);
    FCanvasTileItem Tile(Position - FVector2D(0, Height * .5f), Texture->GetResource(), Size,
        BadgeUV0, BadgeUV1, FLinearColor(1, 1, 1, Opacity));
    Tile.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(Tile);
}

void AERHUD::DrawKillStreak()
{
    const auto* Self = PlayerOwner ? PlayerOwner->GetPlayerState<AERPlayerState>() : nullptr;
    if (!Self || Self->KillStreak.Num() < 2) return;

    const FERHUDLayout Layout(FVector2D(Canvas->SizeX, Canvas->SizeY));
    const int32 Count = Self->KillStreak.Num();
    const float MaxWidth = Layout.Position(760, 0).X;
    constexpr float GapRatio = .18f;
    const float Height = FMath::Min(38.f * Layout.Scale, MaxWidth / (Count * BadgeAspect + (Count - 1) * GapRatio));
    const float Step = Height * (BadgeAspect + GapRatio);
    FVector2D At = Layout.Position(640, 688);
    At.X -= (Count * Step - Height * GapRatio) * .5f;
    for (const uint8 Headshot : Self->KillStreak)
    {
        DrawKillBadge(Headshot != 0, At, Height, 1.f);
        At.X += Step;
    }
}

void AERHUD::DrawKillFeed(const AERGameState* GS, float X, float& Y)
{
    if (!GS || !Canvas || GS->KillFeed.IsEmpty()) return;
    const float S = FERHUDLayout(FVector2D(Canvas->SizeX,Canvas->SizeY)).Scale;
    const double Now = GetWorld()->GetTimeSeconds();
    const auto* Self = PlayerOwner ? PlayerOwner->GetPlayerState<AERPlayerState>() : nullptr;
    const FString SelfName = Self ? Self->GetPlayerName() : FString();
    const float RowHeight = 25.f * S, Pad = 7.f * S, Gap = 6.f * S, TextScale = .74f, IconHeight = 15.f * S;
    bool bDrew = false;
    for (const FERKillFeedEntry& Entry : GS->KillFeed)
    {
        const double Age = Now - Entry.ReceivedAt;
        if (Age < 0 || Age >= KillFeedSeconds) continue;
        const float Opacity = float(FMath::Clamp((KillFeedSeconds - Age) / KillFeedFade, 0.0, 1.0));
        const bool bSuicide = Entry.Killer.IsEmpty();
        const bool bTeamKill = !bSuicide && Entry.KillerTeam == Entry.VictimTeam && Entry.KillerTeam != EERTeam::None;
        const FString Killer = ShortName(Entry.Killer), Victim = ShortName(Entry.Victim);
        // Measure first so the backing box hugs the row.
        const float KillerWidth = bSuicide ? 0.f : MeasureHUDText(Killer, TextScale).X;
        const float TagWidth = bTeamKill ? MeasureHUDText(TEXT("TK"), .55f).X + Gap : 0.f;
        const float IconWidth = WeaponIconWidth(Entry.WeaponKey, IconHeight);
        // Without a catalog icon (fall, world, unknown weapon) a short word takes its place.
        const FString Cause = IconWidth > 0 ? FString() : Entry.bFall ? TEXT("FELL") : bSuicide ? TEXT("DIED") : TEXT("KILLED");
        const float CauseWidth = Cause.IsEmpty() ? 0.f : MeasureHUDText(Cause, .6f).X;
        const float BadgeHeight = 19.f * S;
        const float HeadshotWidth = Entry.bHeadshot && HeadshotBadge ? BadgeHeight * BadgeAspect + Gap : 0.f;
        const float VictimWidth = MeasureHUDText(Victim, TextScale).X;
        const float Width = Pad * 2 + (bSuicide ? 0.f : KillerWidth + Gap + TagWidth) + FMath::Max(IconWidth, CauseWidth) + Gap + HeadshotWidth + VictimWidth;
        const bool bMine = !SelfName.IsEmpty() && (Entry.Killer == SelfName || Entry.Victim == SelfName);
        DrawRect(FLinearColor(0,0,0,(bMine ? .62f : .42f) * Opacity), X, Y, Width, RowHeight);
        if (bMine) DrawRect(FLinearColor(1,1,1,.55f * Opacity), X, Y, 2.f * S, RowHeight);
        const float MidY = Y + RowHeight * .5f;
        float Cursor = X + Pad;
        if (!bSuicide)
        {
            DrawHUDTextAnchored(Killer, FVector2D(Cursor, MidY), NameColor(Entry.KillerTeam).CopyWithNewOpacity(Opacity), TextScale, FVector2D(0,.5f));
            Cursor += KillerWidth + Gap;
            if (bTeamKill)
            {
                DrawHUDTextAnchored(TEXT("TK"), FVector2D(Cursor, MidY), FLinearColor(1.f,.8f,.3f,Opacity), .55f, FVector2D(0,.5f));
                Cursor += TagWidth;
            }
        }
        const float SlotWidth = FMath::Max(IconWidth, CauseWidth);
        if (IconWidth > 0)
        {
            DrawWeaponIcon(Entry.WeaponKey, FVector2D(Cursor, MidY), IconHeight, FLinearColor(1,1,1,.92f * Opacity));
            Cursor += SlotWidth;
        }
        else
        {
            DrawHUDTextAnchored(Cause, FVector2D(Cursor, MidY), FLinearColor(.7f,.72f,.76f,Opacity), .6f, FVector2D(0,.5f));
            Cursor += SlotWidth;
        }
        Cursor += Gap;
        if (HeadshotWidth > 0.f)
        {
            DrawKillBadge(true, FVector2D(Cursor, MidY), BadgeHeight, Opacity);
            Cursor += HeadshotWidth;
        }
        DrawHUDTextAnchored(Victim, FVector2D(Cursor, MidY), NameColor(Entry.VictimTeam).CopyWithNewOpacity(Opacity), TextScale, FVector2D(0,.5f));
        Y += RowHeight + 3.f * S;
        bDrew = true;
    }
    if (bDrew) Y += 6.f * S;
}

void AERHUD::DrawScoreboard(const AERGameState* GS)
{
    if (!GS || !Canvas) return;
    const float W = Canvas->SizeX, H = Canvas->SizeY;
    const FERHUDLayout Layout(FVector2D(W,H));
    const float S = Layout.Scale;
    const float Bottom = H - Layout.Edge().Y;
    const auto* Self = PlayerOwner ? PlayerOwner->GetPlayerState<AERPlayerState>() : nullptr;
    // Two team panels side by side inside the 300-980 reference band, clear of the
    // post-match message column and the minimap.
    const float PanelWidth = 330.f * S, Spacing = 20.f * S;
    const float Top = Layout.Position(0,135).Y;
    const bool bSkullHunt = GS->MatchMode == EERMatchMode::SkullHunt;
    if (bSkullHunt)
        DrawHUDTextAnchored(ERMatchModes::Label(GS->MatchMode), FVector2D(W * .5f, Top - 5.f * S),
            FLinearColor::White, .85f, FVector2D(.5f,1));
    const float HeaderHeight = 46.f * S, ColumnHeight = 26.f * S, RowHeight = 29.f * S;
    for (const EERTeam Team : {EERTeam::Alpha, EERTeam::Omega})
    {
        TArray<const AERPlayerState*> Players;
        for (const APlayerState* Item : GS->PlayerArray)
            if (const auto* State = Cast<AERPlayerState>(Item); State && State->Team == Team) Players.Add(State);
        Players.Sort([bSkullHunt](const AERPlayerState& A, const AERPlayerState& B)
        {
            if (bSkullHunt && A.Skulls != B.Skulls) return A.Skulls > B.Skulls;
            if (A.Kills != B.Kills) return A.Kills > B.Kills;
            if (A.Deaths != B.Deaths) return A.Deaths < B.Deaths;
            return A.GetPlayerName() < B.GetPlayerName();
        });
        const bool bAlpha = Team == EERTeam::Alpha;
        const FLinearColor Color = ERRecon::TeamColor(Team);
        const float Left = W * .5f + (bAlpha ? -Spacing * .5f - PanelWidth : Spacing * .5f);
        const int32 Fit = FMath::Max(1, FMath::FloorToInt32((Bottom - Top - HeaderHeight - ColumnHeight) / RowHeight));
        const int32 Shown = FMath::Min(Players.Num(), Players.Num() > Fit ? Fit - 1 : Fit);
        const int32 Rows = Shown + (Players.Num() > Shown ? 1 : 0);
        const float PanelHeight = HeaderHeight + ColumnHeight + FMath::Max(1, Rows) * RowHeight + 6.f * S;
        DrawRect(FLinearColor(.015f,.025f,.04f,.9f), Left, Top, PanelWidth, PanelHeight);
        // Header: team name and score on a team-coloured band.
        DrawRect(FLinearColor(Color.R*.55f, Color.G*.55f, Color.B*.55f, .95f), Left, Top, PanelWidth, HeaderHeight);
        DrawRect(Color, Left, Top + HeaderHeight - 3.f * S, PanelWidth, 3.f * S);
        const float HeaderMid = Top + HeaderHeight * .5f;
        DrawHUDTextAnchored(bAlpha ? TEXT("ALPHA") : TEXT("OMEGA"), FVector2D(Left + 16.f * S, HeaderMid), FLinearColor::White, 1.25f, FVector2D(0,.5f));
        DrawHUDTextAnchored(FString::FromInt(bAlpha ? GS->AlphaScore : GS->OmegaScore), FVector2D(Left + PanelWidth - 16.f * S, HeaderMid),
            FLinearColor::White, 1.6f, FVector2D(1,.5f));
        // Column captions; numbers are centred under their captions.
        const float Columns[] = {bSkullHunt ? 193.f : 226.f, bSkullHunt ? 223.f : 266.f, bSkullHunt ? 253.f : 306.f, 294.f};
        const int32 ColumnCount = bSkullHunt ? 4 : 3;
        const float CaptionY = Top + HeaderHeight + ColumnHeight * .5f;
        const FLinearColor Caption(.6f,.64f,.7f);
        DrawHUDTextAnchored(TEXT("PLAYER"), FVector2D(Left + 18.f * S, CaptionY), Caption, .6f, FVector2D(0,.5f));
        const TCHAR* Captions[] = {TEXT("K"), TEXT("D"), TEXT("TK"), TEXT("SKULLS")};
        for (int32 I = 0; I < ColumnCount; ++I)
            DrawHUDTextAnchored(Captions[I], FVector2D(Left + Columns[I] * S, CaptionY), Caption, .6f, FVector2D(.5f,.5f));
        float Y = Top + HeaderHeight + ColumnHeight;
        if (Players.IsEmpty())
            DrawHUDTextAnchored(TEXT("No players"), FVector2D(Left + 18.f * S, Y + RowHeight * .5f), FLinearColor(.5f,.53f,.58f), .7f, FVector2D(0,.5f));
        for (int32 Index = 0; Index < Shown; ++Index)
        {
            const AERPlayerState* State = Players[Index];
            const bool bSelf = State == Self;
            if (bSelf) DrawRect(FLinearColor(Color.R, Color.G, Color.B, .22f), Left, Y, PanelWidth, RowHeight);
            else if ((Index % 2) != 0) DrawRect(FLinearColor(1,1,1,.035f), Left, Y, PanelWidth, RowHeight);
            DrawRect(Color.CopyWithNewOpacity(bSelf ? 1.f : .7f), Left, Y + 3.f * S, 4.f * S, RowHeight - 6.f * S);
            const float Mid = Y + RowHeight * .5f;
            const FVector2D NameSize = DrawHUDTextAnchored(ShortName(State->GetPlayerName(), (State->IsABot() ? 14 : 18) - (bSkullHunt ? 3 : 0)), FVector2D(Left + 18.f * S, Mid),
                bSelf ? FLinearColor::White : FLinearColor(.88f,.9f,.94f), .8f, FVector2D(0,.5f));
            if (State->IsABot())
                DrawHUDTextAnchored(TEXT("BOT"), FVector2D(Left + 24.f * S + NameSize.X, Mid), FLinearColor(.55f,.58f,.64f), .5f, FVector2D(0,.5f));
            const int32 Values[] = {State->Kills, State->Deaths, State->TeamKills, State->Skulls};
            for (int32 I = 0; I < ColumnCount; ++I)
                DrawHUDTextAnchored(FString::FromInt(Values[I]), FVector2D(Left + Columns[I] * S, Mid),
                    I == 2 && Values[I] > 0 ? FLinearColor(1.f,.8f,.3f) : FLinearColor::White, .8f, FVector2D(.5f,.5f));
            Y += RowHeight;
        }
        if (Players.Num() > Shown)
            DrawHUDTextAnchored(FString::Printf(TEXT("+%d more"), Players.Num() - Shown), FVector2D(Left + 18.f * S, Y + RowHeight * .5f),
                FLinearColor(.6f,.64f,.7f), .7f, FVector2D(0,.5f));
    }
}

void AERHUD::DrawKillerHealth(const AERCharacter* Victim)
{
    const AERCharacter* Killer = Victim ? Victim->DeathFocus.Get() : nullptr;
    if (!Canvas || !IsValid(Killer) || !Killer->Health || Killer->Health->bDead) return;
    const FERHUDLayout Layout(FVector2D(Canvas->SizeX,Canvas->SizeY));
    const float S = Layout.Scale;
    const FVector Head = Killer->GetActorLocation() + FVector(0,0,Killer->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 10.f);
    const FVector Screen = Canvas->Project(Head, true);
    if (Screen.Z <= 0) return;
    // Same health and armor icons and sector rules as the owner's vitals, at a smaller size.
    const float HealthFraction = Killer->Health->Health / FMath::Max(1.f, ERServer::Get(GetWorld(),"Health.Max"));
    const float ArmorFraction = Killer->Health->Armor / FMath::Max(1.f, ERServer::Get(GetWorld(),"Health.Armor"));
    const float Diameter = 30.f * S, IconGap = 3.f * S, Gap = 8.f * S;
    const auto* KillerState = Killer->GetPlayerState<AERPlayerState>();
    const FString Name = KillerState ? KillerState->GetPlayerName().Left(20) : FString();
    const float NameWidth = Name.IsEmpty() ? 0.f : MeasureHUDText(Name, .72f).X + Gap;
    const float RowWidth = NameWidth + 2.f * Diameter + IconGap;
    // One row just above the head, kept below the top-centre score, timer and network
    // status, and inside the screen edges.
    const float MidY = FMath::Clamp(float(Screen.Y) - 8.f * S - Diameter * .5f, Layout.Position(0,118).Y, Canvas->SizeY - 120.f * S);
    float X = FMath::Clamp(float(Screen.X) - RowWidth * .5f, 8.f * S, Canvas->SizeX - RowWidth - 8.f * S);
    DrawRect(FLinearColor(0,0,0,.45f), X - 6.f * S, MidY - Diameter * .5f - 3.f * S, RowWidth + 12.f * S, Diameter + 6.f * S);
    if (KillerState)
        DrawHUDTextAnchored(Name, FVector2D(X, MidY), ERRecon::TeamColor(KillerState->Team), .72f, FVector2D(0,.5f));
    X += NameWidth;
    DrawVitalIcon(0, FVector2D(X, MidY - Diameter * .5f), Diameter, HealthFraction);
    DrawVitalIcon(1, FVector2D(X + Diameter + IconGap, MidY - Diameter * .5f), Diameter, ArmorFraction);
}
