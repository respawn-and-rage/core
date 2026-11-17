#include "ERMatchGraph.h"
#include "ERInterfaceStyle.h"
#include "ERMapCatalog.h"
#include "ERPlayerController.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Dom/JsonObject.h"
#include "Engine/GameViewportClient.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
const FLinearColor MatchGraphAlphaTeamColor(.34f, .65f, 1.f);
const FLinearColor MatchGraphOmegaTeamColor(1.f, .52f, .26f);
const FLinearColor Muted(.62f, .69f, .77f);

const FLinearColor Positive = FLinearColor::FromSRGBColor(FColor(0, 128, 0));
const FLinearColor Negative = FLinearColor::FromSRGBColor(FColor(255, 0, 0));
const FLinearColor BestOutline = FLinearColor::FromSRGBColor(FColor(255, 215, 0));
const FLinearColor GraphAlpha(.035f, .17f, .35f);
const FLinearColor GraphOmega(.36f, .10f, .025f);
constexpr int32 PlayerLabelSize = 13;

struct FPlayer
{
    int32 Id = INDEX_NONE;
    FString Name;
    FString Team;
    FLinearColor Color;
    int32 Kills = 0;
    int32 Deaths = 0;
    int32 TeamKills = 0;

    int32 TotalKills() const { return Kills + TeamKills; }
    int32 Balance() const { return TotalKills() - Deaths; }
};

struct FEdge
{
    int32 From = INDEX_NONE;
    int32 To = INDEX_NONE;
    int32 Hits = 0;
    int32 Kills = 0;
};

struct FMatchChoice
{
    FString Id;
    FString Label;
    int32 AlphaScore = 0;
    int32 OmegaScore = 0;
};

const TArray<TSharedPtr<FJsonValue>>& ArrayField(const FJsonObject& Json, const TCHAR* Key)
{
    static const TArray<TSharedPtr<FJsonValue>> Empty;
    const TArray<TSharedPtr<FJsonValue>>* Result = nullptr;
    return Json.TryGetArrayField(Key, Result) ? *Result : Empty;
}

FString MapLabel(const FString& Map)
{
    for (const auto& Entry : ERMaps::All())
        if (FPackageName::GetShortName(Entry.Package) == Map) return Entry.Label;
    return TEXT("Custom map");
}

TSharedRef<STextBlock> Label(const FString& Text, int32 Size, FLinearColor Color = FLinearColor::White)
{
    return SNew(STextBlock).Font(ERInterface::Font(Size)).ColorAndOpacity(Color).Text(FText::FromString(Text));
}

class SCombatGraph : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SCombatGraph) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&) {}
    void SetData(const TArray<FPlayer>& InPlayers, const TArray<FEdge>& InEdges)
    {
        Players = InPlayers;
        Players.Sort([](const FPlayer& A, const FPlayer& B)
        {
            return A.Team == B.Team ? A.Id < B.Id : A.Team < B.Team;
        });
        Edges = InEdges;
        Highlight = Pinned = INDEX_NONE;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
    void Select(int32 Player)
    {
        Pinned = Pinned == Player ? INDEX_NONE : Player;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
    int32 Focused() const { return Highlight != INDEX_NONE ? Highlight : Pinned; }

    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(820, 420); }
    virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        const int32 Previous = Highlight;
        Highlight = HitNode(Geometry, Event);
        if (Previous != Highlight) Invalidate(EInvalidateWidgetReason::Paint);
        return FReply::Unhandled();
    }
    virtual void OnMouseLeave(const FPointerEvent& Event) override
    {
        Highlight = INDEX_NONE;
        Invalidate(EInvalidateWidgetReason::Paint);
        SLeafWidget::OnMouseLeave(Event);
    }
    virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (Event.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
        Select(HitNode(Geometry, Event));
        return FReply::Handled();
    }

    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
        FSlateWindowElementList& Draw, int32 Layer, const FWidgetStyle&, bool) const override
    {
        if (Players.IsEmpty()) return Layer;
        const FLayout Layout = BuildLayout(Geometry.GetLocalSize());
        const auto* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
        const int32 Focus = Focused();

        // csgo-balls uses two superimposed strokes. Both stop two thirds of the
        // way from source to target, so the opposite direction has its own start.
        for (const bool bKills : {false, true})
        {
            for (const auto& Edge : Edges)
            {
                const int32 Count = bKills ? Edge.Kills : Edge.Hits;
                const auto* Start = Layout.Nodes.Find(Edge.From);
                const auto* End = Layout.Nodes.Find(Edge.To);
                if (Count <= 0 || !Start || !End) continue;
                const FVector2f Segment = (End->Center - Start->Center) * (2.f / 3.f);
                const float Width = float(Count) * Layout.Scale;
                const FVector2f Pivot(0.f, Width * .5f);
                FSlateDrawElement::MakeRotatedBox(Draw, Layer + (bKills ? 2 : 1),
                    Geometry.ToPaintGeometry(FVector2f(Segment.Size(), Width), FSlateLayoutTransform(Start->Center - Pivot)),
                    WhiteBrush, ESlateDrawEffect::NoPixelSnapping, FMath::Atan2(Segment.Y, Segment.X),
                    Pivot, FSlateDrawElement::RelativeToElement, FLinearColor::Black.CopyWithNewOpacity(bKills ? .2f : .04f));
            }
        }

        static const FSlateRoundedBoxBrush Disk(FLinearColor::White);
        const auto DrawDisk = [&](FVector2f Center, float Radius, FLinearColor Color, int32 DrawLayer)
        {
            FSlateDrawElement::MakeBox(Draw, DrawLayer,
                Geometry.ToPaintGeometry(FVector2f(Radius * 2.f), FSlateLayoutTransform(Center - FVector2f(Radius))),
                &Disk, ESlateDrawEffect::None, Color);
        };
        for (const auto& Player : Players)
        {
            const FNode& Node = Layout.Nodes[Player.Id];
            if (Node.Radius > 0)
            {
                // Like the CSS rule order, the low outline takes precedence if everyone ties.
                if (Player.Balance() == Layout.Lowest || Player.Balance() == Layout.Highest)
                    DrawDisk(Node.Center, Node.Radius + 1.5f * Layout.Scale,
                        Player.Balance() == Layout.Lowest ? FLinearColor::Black : BestOutline, Layer + 3);
                DrawDisk(Node.Center, Node.Radius, Player.Balance() >= 0 ? Positive : Negative, Layer + 4);
            }
            const FLinearColor LabelColor = Player.Team == TEXT("alpha") ? GraphAlpha
                : Player.Team == TEXT("omega") ? GraphOmega : FLinearColor::Black;
            FSlateDrawElement::MakeText(Draw, Layer + 5,
                Geometry.ToPaintGeometry(Node.LabelSize, FSlateLayoutTransform(Node.LabelOrigin)),
                Node.Text, ERInterface::Font(PlayerLabelSize), ESlateDrawEffect::None, Focus == Player.Id ? FLinearColor::Black : LabelColor);
        }

        // The original displays the two directed counts only for a lethal link
        // incident to the hovered player. Hits without a kill still draw their stroke.
        if (Focus != INDEX_NONE)
        {
            const auto Font = ERInterface::Font(12);
            const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
            for (const auto& Edge : Edges)
            {
                if (Edge.Kills <= 0 || (Edge.From != Focus && Edge.To != Focus)) continue;
                const auto* Start = Layout.Nodes.Find(Edge.From);
                const auto* End = Layout.Nodes.Find(Edge.To);
                if (!Start || !End) continue;
                const FString Counts = FString::Printf(TEXT("%d K · %d H"), Edge.Kills, Edge.Hits);
                const FVector2f TextSize(Measure->Measure(Counts, Font));
                const FVector2f Tip = FMath::Lerp(Start->Center, End->Center, 2.f / 3.f);
                const FVector2f TextOrigin = Tip - TextSize * .5f;
                FSlateDrawElement::MakeBox(Draw, Layer + 6,
                    Geometry.ToPaintGeometry(TextSize + FVector2f(8, 0), FSlateLayoutTransform(TextOrigin - FVector2f(4, 0))),
                    WhiteBrush, ESlateDrawEffect::None, FLinearColor(1, 1, 1, .9f));
                FSlateDrawElement::MakeText(Draw, Layer + 7,
                    Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(TextOrigin)),
                    Counts, Font, ESlateDrawEffect::None, FLinearColor::Black);
            }
        }
        return Layer + 7;
    }

private:
    struct FNode
    {
        FVector2f Center;
        FVector2f LabelOrigin;
        FVector2f LabelSize;
        FString Text;
        float Radius = 0;
    };

    struct FLayout
    {
        TMap<int32, FNode> Nodes;
        float Scale = 1;
        int32 Lowest = MAX_int32;
        int32 Highest = MIN_int32;
    };

    FLayout BuildLayout(FVector2D Size) const
    {
        FLayout Layout;
        for (const auto& Player : Players)
        {
            Layout.Lowest = FMath::Min(Layout.Lowest, Player.Balance());
            Layout.Highest = FMath::Max(Layout.Highest, Player.Balance());
        }
        if (Players.IsEmpty()) return Layout;

        // Keep the original orbit of 200 units and fit it and the largest
        // circle together. The same scale applies to radii and stroke widths.
        const int32 LargestRadius = FMath::Max(FMath::Abs(Layout.Lowest), FMath::Abs(Layout.Highest));
        const float Diameter = FMath::Max(465.f, 2.f * (210.f + float(LargestRadius)));
        Layout.Scale = FMath::Max(.01f, FMath::Min((float(Size.X) - 210.f) / Diameter,
            (float(Size.Y) - 28.f) / Diameter));
        const FVector2f Origin = FVector2f(Size) * .5f;
        const auto Font = ERInterface::Font(PlayerLabelSize);
        const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        for (int32 Index = 0; Index < Players.Num(); ++Index)
        {
            const auto& Player = Players[Index];
            const float Angle = 2.f * UE_PI * (float(Index) + .5f) / float(Players.Num()) + UE_PI * .5f;
            const FVector2f Outward(FMath::Cos(Angle), -FMath::Sin(Angle));
            FNode Node;
            Node.Center = Origin + Outward * (200.f * Layout.Scale);
            Node.Radius = float(FMath::Abs(Player.Balance())) * Layout.Scale;
            const FString Name = Player.Name.Len() > 16 ? Player.Name.Left(15) + TEXT("…") : Player.Name;
            Node.Text = FString::Printf(TEXT("%s: %d"), *Name, Player.Balance());
            Node.LabelSize = FVector2f(Measure->Measure(Node.Text, Font));
            const FVector2f Anchor = Node.Center + Outward * (Node.Radius + 10.f * Layout.Scale);
            Node.LabelOrigin = Anchor - FVector2f(Outward.X < 0 ? Node.LabelSize.X : 0.f, Node.LabelSize.Y * .5f);
            Layout.Nodes.Add(Player.Id, MoveTemp(Node));
        }
        return Layout;
    }

    int32 HitNode(const FGeometry& Geometry, const FPointerEvent& Event) const
    {
        const FVector2f Mouse(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
        const FLayout Layout = BuildLayout(Geometry.GetLocalSize());
        int32 Best = INDEX_NONE;
        float Distance = MAX_flt;
        for (const auto& Pair : Layout.Nodes)
        {
            const auto& Node = Pair.Value;
            const FVector2f LabelOffset = Mouse - Node.LabelOrigin;
            const bool bInLabel = LabelOffset.X >= 0 && LabelOffset.X <= Node.LabelSize.X &&
                LabelOffset.Y >= 0 && LabelOffset.Y <= Node.LabelSize.Y;
            const float Candidate = FVector2f::DistSquared(Node.Center, Mouse);
            const bool bInCircle = Node.Radius > 0 && Candidate <= FMath::Square(FMath::Max(8.f, Node.Radius));
            if (!bInLabel && !bInCircle) continue;
            if (Candidate >= Distance) continue;
            Distance = Candidate;
            Best = Pair.Key;
        }
        return Best;
    }

    TArray<FPlayer> Players;
    TArray<FEdge> Edges;
    int32 Highlight = INDEX_NONE;
    int32 Pinned = INDEX_NONE;
};

class SMatchGraph : public SCompoundWidget
{
    using FDayPtr = TSharedPtr<FString>;
    using FMatchPtr = TSharedPtr<FMatchChoice>;
public:
    SLATE_BEGIN_ARGS(SMatchGraph) {}
        SLATE_ARGUMENT(TWeakObjectPtr<AERPlayerController>, PlayerController)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        PC = Args._PlayerController;
        ChildSlot
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [
                SNew(SHorizontalBox).IsEnabled_Lambda([this] { return !bLoading; })
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
                [
                    SAssignNew(DayCombo, SComboBox<FDayPtr>).OptionsSource(&Days).ContentPadding(FMargin(7, 2))
                    .OnGenerateWidget_Lambda([](FDayPtr Item) -> TSharedRef<SWidget> { return Label(*Item, 15); })
                    .OnSelectionChanged_Lambda([this](FDayPtr Item, ESelectInfo::Type Why)
                    { if (Item && Why != ESelectInfo::Direct) Load(*Item, {}); })
                    [SNew(STextBlock).Font(ERInterface::Font(15)).Text_Lambda([this]
                    { return FText::FromString(Day.IsEmpty() ? TEXT("Match date") : Day); })]
                ]
                + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 10, 0)
                [
                    SAssignNew(MatchCombo, SComboBox<FMatchPtr>).OptionsSource(&Matches).ContentPadding(FMargin(7, 2))
                    .OnGenerateWidget_Lambda([](FMatchPtr Item) -> TSharedRef<SWidget> { return Label(Item->Label, 15); })
                    .OnSelectionChanged_Lambda([this](FMatchPtr Item, ESelectInfo::Type Why)
                    { if (Item && Why != ESelectInfo::Direct) Load(Day, Item->Id); })
                    [SNew(STextBlock).Font(ERInterface::Font(15)).Text_Lambda([this]
                    { return FText::FromString(Selected ? Selected->Label : TEXT("Match")); })]
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton).ContentPadding(FMargin(8, 2))
                    .OnClicked_Lambda([this] { Load({}, {}); return FReply::Handled(); })
                    [Label(TEXT("Latest match"), 15)]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [
                SNew(STextBlock).Font(ERInterface::Font(14)).ColorAndOpacity(Muted)
                .Visibility_Lambda([this] { return Status.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
                .Text_Lambda([this] { return FText::FromString(Status); })
            ]
            + SVerticalBox::Slot().FillHeight(1)
            [
                SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(.78f, .8f, .83f, 1.f)).Padding(8)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1)[Label(TEXT("ALPHA"), 17, GraphAlpha)]
                        + SHorizontalBox::Slot().AutoWidth().Padding(6, 0)
                            [Label(TEXT("Hits · faint"), 14, FLinearColor(.24f, .26f, .28f))]
                        + SHorizontalBox::Slot().AutoWidth().Padding(6, 0)
                            [Label(TEXT("Kills · dark"), 14, FLinearColor(.075f, .08f, .09f))]
                        + SHorizontalBox::Slot().FillWidth(1).HAlign(HAlign_Right)[Label(TEXT("OMEGA"), 17, GraphOmega)]
                    ]
                    + SVerticalBox::Slot().FillHeight(1)[SAssignNew(Graph, SCombatGraph).Clipping(EWidgetClipping::ClipToBounds)]
                    + SVerticalBox::Slot().AutoHeight()[SAssignNew(DayScores, SVerticalBox)]
                ]
            ]
        ];
        Load({}, {});
    }

private:
    void UpdateDayScores()
    {
        DayScores->ClearChildren();
        if (Matches.IsEmpty()) return;
        int64 AlphaTotal = 0, OmegaTotal = 0, AlphaSpan = 0, OmegaSpan = 0;
        for (const auto& Match : Matches)
        {
            AlphaTotal += Match->AlphaScore;
            OmegaTotal += Match->OmegaScore;
            AlphaSpan += FMath::Max(0, Match->AlphaScore);
            OmegaSpan += FMath::Max(0, Match->OmegaScore);
        }
        const int64 MaxSpan = FMath::Max(AlphaSpan, OmegaSpan);
        for (const bool bAlpha : {true, false})
        {
            const auto Color = bAlpha ? GraphAlpha : GraphOmega;
            const auto Total = bAlpha ? AlphaTotal : OmegaTotal;
            const auto Span = bAlpha ? AlphaSpan : OmegaSpan;
            auto Bar = SNew(SHorizontalBox);
            for (int32 Index = Matches.Num() - 1; Index >= 0; --Index)
            {
                const auto& Match = Matches[Index];
                const int32 Score = bAlpha ? Match->AlphaScore : Match->OmegaScore;
                if (Score <= 0) continue;
                Bar->AddSlot().FillWidth(float(Score)).Padding(0, 0, 1, 0)
                [
                    SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(Color.CopyWithNewOpacity(Match == Selected ? 1.f : .63f))
                    .ToolTipText(FText::FromString(Match->Label)).Clipping(EWidgetClipping::ClipToBounds)
                    .Padding(0).HAlign(HAlign_Center)
                    [Label(FString::FromInt(Score), 11)]
                ];
            }
            if (Span < MaxSpan)
                Bar->AddSlot().FillWidth(float(MaxSpan - Span))[SNew(SBox)];
            DayScores->AddSlot().AutoHeight().Padding(0, 1)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SBox).WidthOverride(76)
                    [Label(FString::Printf(TEXT("%s  %lld"), bAlpha ? TEXT("Alpha") : TEXT("Omega"), Total), 12, Color)]
                ]
                + SHorizontalBox::Slot().FillWidth(1)[Bar]
            ];
        }
    }

    void Load(const FString& InDay, const FString& Match)
    {
        if (bLoading || !PC.IsValid()) return;
        bLoading = true;
        Status = TEXT("Loading match statistics…");
        PC->RequestMatchGraph(InDay, Match, [Weak = TWeakPtr<SMatchGraph>(SharedThis(this))](const FString& Json)
        {
            if (auto Widget = Weak.Pin()) Widget->Receive(Json);
        });
    }

    void Receive(const FString& Text)
    {
        bLoading = false;
        TSharedPtr<FJsonObject> Json;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json)
        {
            Status = TEXT("Match statistics are unavailable.");
            return;
        }
        if (Json->TryGetStringField(TEXT("error"), Status)) return;
        Days.Reset();
        Matches.Reset();
        Selected.Reset();
        Players.Reset();
        Edges.Reset();
        Day.Reset();
        Json->TryGetStringField(TEXT("day"), Day);
        FString ChosenId;
        Json->TryGetStringField(TEXT("selected"), ChosenId);
        for (const auto& Value : ArrayField(*Json, TEXT("days"))) Days.Add(MakeShared<FString>(Value->AsString()));
        for (const auto& Value : ArrayField(*Json, TEXT("matches")))
        {
            if (!Value || Value->Type != EJson::Object || !Value->AsObject()) continue;
            const auto& Row = *Value->AsObject();
            auto Item = MakeShared<FMatchChoice>();
            Item->Id = Row.GetStringField(TEXT("match_id"));
            const FString Time = Row.GetStringField(TEXT("match_ended_at")).Mid(11, 8);
            Item->AlphaScore = Row.GetIntegerField(TEXT("alpha_score"));
            Item->OmegaScore = Row.GetIntegerField(TEXT("omega_score"));
            Item->Label = FString::Printf(TEXT("%s   ·   %s UTC   ·   %d–%d"),
                *MapLabel(Row.GetStringField(TEXT("map"))), *Time, Item->AlphaScore, Item->OmegaScore);
            if (Item->Id == ChosenId) Selected = Item;
            Matches.Add(MoveTemp(Item));
        }
        for (const auto& Value : ArrayField(*Json, TEXT("players")))
        {
            if (!Value || Value->Type != EJson::Object || !Value->AsObject()) continue;
            const auto& Row = *Value->AsObject();
            FPlayer& Player = Players.AddDefaulted_GetRef();
            Player.Id = Row.GetIntegerField(TEXT("id"));
            Player.Name = Row.GetStringField(TEXT("name"));
            if (Row.GetBoolField(TEXT("bot"))) Player.Name += TEXT(" · BOT");
            Player.Team = Row.GetStringField(TEXT("team"));
            Player.Color = Player.Team == TEXT("alpha") ? MatchGraphAlphaTeamColor : Player.Team == TEXT("omega") ? MatchGraphOmegaTeamColor : Muted;
            Player.Kills = Row.GetIntegerField(TEXT("kills"));
            Player.Deaths = Row.GetIntegerField(TEXT("deaths"));
            Player.TeamKills = Row.GetIntegerField(TEXT("team_kills"));
        }
        for (const auto& Value : ArrayField(*Json, TEXT("edges")))
        {
            if (!Value || Value->Type != EJson::Object || !Value->AsObject()) continue;
            const auto& Row = *Value->AsObject();
            Edges.Add({Row.GetIntegerField(TEXT("from")), Row.GetIntegerField(TEXT("to")),
                Row.GetIntegerField(TEXT("hits")), Row.GetIntegerField(TEXT("kills"))});
        }
        DayCombo->RefreshOptions();
        MatchCombo->RefreshOptions();
        Graph->SetData(Players, Edges);
        UpdateDayScores();
        Status = Selected ? FString() : TEXT("No completed matches with combat events.");
#if !UE_BUILD_SHIPPING
        if (FParse::Param(FCommandLine::Get(), TEXT("ERMatchGraphProbe")))
        {
            int32 FocusId = INDEX_NONE;
            if (FParse::Value(FCommandLine::Get(), TEXT("ERMatchGraphFocus="), FocusId))
                Graph->Select(FocusId);
            UE_LOG(LogTemp, Display, TEXT("ER_MATCH_GRAPH_READY selected=%d matches=%d players=%d edges=%d"),
                Selected.IsValid() ? 1 : 0, Matches.Num(), Players.Num(), Edges.Num());
            FString Capture;
            if (FApp::CanEverRender() && FParse::Value(FCommandLine::Get(), TEXT("ERMatchGraphCapture="), Capture))
                FScreenshotRequest::RequestScreenshot(Capture, true, false);
        }
#endif
    }

    TWeakObjectPtr<AERPlayerController> PC;
    TSharedPtr<SComboBox<FDayPtr>> DayCombo;
    TSharedPtr<SComboBox<FMatchPtr>> MatchCombo;
    TSharedPtr<SCombatGraph> Graph;
    TSharedPtr<SVerticalBox> DayScores;
    TArray<FDayPtr> Days;
    TArray<FMatchPtr> Matches;
    FMatchPtr Selected;
    FString Day;
    FString Status;
    TArray<FPlayer> Players;
    TArray<FEdge> Edges;
    bool bLoading = false;
};
}

TSharedRef<SWidget> ERMatchGraph::Create(AERPlayerController* Controller)
{
    return SNew(SMatchGraph).PlayerController(Controller);
}
