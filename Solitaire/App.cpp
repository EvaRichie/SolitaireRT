﻿#include "pch.h"
#include "Card.h"
#include "ShapeCache.h"
#include "CompositionCard.h"
#include "Pack.h"
#include "CardStack.h"
#include "Waste.h"
#include "Deck.h"
#include "Foundation.h"

using namespace winrt;

using namespace Windows;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI;
using namespace Windows::UI::Core;
using namespace Windows::UI::Composition;
using namespace Windows::UI::Popups;

enum class HitTestZone
{
    None,
    Deck,
    Waste,
    Foundations,
    PlayArea
};

struct App : implements<App, IFrameworkViewSource, IFrameworkView>
{
    CompositionTarget m_target{ nullptr };
    ContainerVisual m_root{ nullptr };
    ContainerVisual m_boardLayer{ nullptr };
    ContainerVisual m_foundationVisual{ nullptr };
    ContainerVisual m_deckVisual{ nullptr };
    ContainerVisual m_wasteVisual{ nullptr };
    ContainerVisual m_playAreaVisual{ nullptr };
    ContainerVisual m_selectedLayer{ nullptr };
    VisualCollection m_visuals{ nullptr };

    Visual m_selectedVisual{ nullptr };
    Pile::CardList m_selectedCards;
    Pile::ItemContainerList m_selectedItemContainers;
    Pile::RemovalOperation m_lastOperation;
    std::shared_ptr<Pile> m_lastPile;
    Pile::HitTestResult m_lastHitTest;
    float2 m_offset{};

    std::shared_ptr<ShapeCache> m_shapeCache;
    std::unique_ptr<Pack> m_pack;
    std::vector<std::shared_ptr<CardStack>> m_stacks;
    std::map<HitTestZone, Rect> m_zoneRects;
    std::unique_ptr<Deck> m_deck;
    std::shared_ptr<Waste> m_waste;
    std::vector<std::shared_ptr<::Foundation>> m_foundations;

    IFrameworkView CreateView()
    {
        return *this;
    }

    void Initialize(CoreApplicationView const &)
    {
    }

    void Load(hstring const&)
    {
    }

    void Uninitialize()
    {
    }

    void Run()
    {
        CoreWindow window = CoreWindow::GetForCurrentThread();
        window.Activate();

        CoreDispatcher dispatcher = window.Dispatcher();
        dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessUntilQuit);
    }

    void SetWindow(CoreWindow const & window)
    {
        Compositor compositor;
        // Base visual tree
        m_shapeCache = std::make_shared<ShapeCache>(compositor);
        m_root = compositor.CreateContainerVisual();
        m_root.RelativeSizeAdjustment({ 1, 1 });
        m_target = compositor.CreateTargetForCurrentView();
        m_target.Root(m_root);

        m_boardLayer = compositor.CreateContainerVisual();
        m_boardLayer.RelativeSizeAdjustment({ 1, 1 });
        m_root.Children().InsertAtTop(m_boardLayer);

        m_selectedLayer = compositor.CreateContainerVisual();
        m_selectedLayer.RelativeSizeAdjustment({ 1, 1 });
        m_root.Children().InsertAtTop(m_selectedLayer);

        m_visuals = m_boardLayer.Children();

        // Init cards
        m_pack = std::make_unique<Pack>(m_shapeCache);
#ifdef _DEBUG
        m_pack->Shuffle({ 1318857190, 1541316502, 3202618166, 965450609 });
        //m_pack->Shuffle();
#else
        m_pack->Shuffle();
#endif
        auto cards = m_pack->Cards();
        auto textHeight = m_shapeCache->TextHeight();

        const auto cardSize = CompositionCard::CardSize;

        // Play Area
        auto playAreaOffsetY = cardSize.y + 25.0f;
        m_playAreaVisual = compositor.CreateContainerVisual();
        m_playAreaVisual.Offset({ 0, playAreaOffsetY, 0 });
        m_playAreaVisual.Size({ 0, -playAreaOffsetY });
        m_playAreaVisual.RelativeSizeAdjustment({ 1, 1 });
        m_visuals.InsertAtTop(m_playAreaVisual);
        auto playAreaVisuals = m_playAreaVisual.Children();
        auto cardsSoFar = 0;
        auto numberOfStacks = 7;
        for (int i = 0; i < numberOfStacks; i++)
        {
            auto numberOfCards = i + 1;

            auto start = cards.begin() + cardsSoFar;
            std::vector<std::shared_ptr<CompositionCard>> tempStack(start, start + numberOfCards);
            cardsSoFar += numberOfCards;

            auto stack = std::make_shared<CardStack>(m_shapeCache, tempStack);
            stack->SetLayoutOptions(textHeight);
            stack->ForceLayout();
            auto baseVisual = stack->Base();

            baseVisual.Offset({ (float)i * (cardSize.x + 15.0f), 0, 0 });
            playAreaVisuals.InsertAtTop(baseVisual);

            m_stacks.push_back(stack);
        }
        for (auto& stack : m_stacks)
        {
            auto cards = stack->Cards();
            for (auto& card : cards)
            {
                card->IsFaceUp(false);
            }
            cards.back()->IsFaceUp(true);
        }
        m_zoneRects.insert({ HitTestZone::PlayArea, { 0, playAreaOffsetY, window.Bounds().Width, window.Bounds().Height - playAreaOffsetY } });

        // Deck
        std::vector<std::shared_ptr<CompositionCard>> deck(cards.begin() + cardsSoFar, cards.end());
        m_deck = std::make_unique<Deck>(m_shapeCache, deck);
        m_deck->ForceLayout();
        m_visuals.InsertAtTop(m_deck->Base());
        m_zoneRects.insert({ HitTestZone::Deck, { 0, 0, cardSize.x, cardSize.y } });

        // Waste
        m_wasteVisual = compositor.CreateContainerVisual();
        m_wasteVisual.Size({ (2.0f * 65.0f) + cardSize.x, cardSize.y });
        m_wasteVisual.Offset({ cardSize.x + 25.0f, 0, 0 });
        m_waste = std::make_shared<Waste>(m_shapeCache);
        m_waste->SetLayoutOptions(65.0f);
        m_waste->ForceLayout();
        m_wasteVisual.Children().InsertAtTop(m_waste->Base());
        m_visuals.InsertAtTop(m_wasteVisual);
        m_zoneRects.insert({ HitTestZone::Waste, { cardSize.x + 25.0f, 0, (2.0f * 65.0f) + cardSize.x, cardSize.y } });

        // Foundation
        m_foundationVisual = compositor.CreateContainerVisual();
        m_foundationVisual.Size({ 4.0f * cardSize.x + 3.0f * 15.0f, cardSize.y });
        m_foundationVisual.AnchorPoint({ 1, 0 });
        m_foundationVisual.RelativeOffsetAdjustment({ 1, 0, 0 });
        m_visuals.InsertAtTop(m_foundationVisual);
        for (int i = 0; i < 4; i++)
        {
            auto foundation = std::make_shared<::Foundation>(m_shapeCache);
            auto visual = foundation->Base();
            visual.Offset({i * (cardSize.x + 15.0f), 0, 0 });
            m_foundationVisual.Children().InsertAtTop(visual);
            m_foundations.push_back(foundation);
        }
        m_zoneRects.insert({ HitTestZone::Foundations, { window.Bounds().Width - m_foundationVisual.Size().x, 0, m_foundationVisual.Size().x, m_foundationVisual.Size().y } });

        window.PointerPressed({ this, &App::OnPointerPressed });
        window.PointerMoved({ this, &App::OnPointerMoved });
        window.PointerReleased({ this, &App::OnPointerReleased });
        window.SizeChanged({ this, &App::OnSizeChanged });
    }

    void OnPointerPressed(IInspectable const &, PointerEventArgs const & args)
    {
        float2 const point = args.CurrentPoint().Position();

        for (auto& pair : m_zoneRects)
        {
            auto zoneType = pair.first;
            auto rect = pair.second;

            if (point.x >= rect.X &&
                point.x < rect.X + rect.Width &&
                point.y >= rect.Y &&
                point.y < rect.Y + rect.Height)
            {
                switch (zoneType)
                {
                case HitTestZone::Deck:
                {
                    if (m_deck->HitTest(point))
                    {
                        auto cards = m_deck->Draw();

                        if (!cards.empty())
                        {
                            m_waste->Discard(cards);
                            //m_waste->ForceLayout(65.0f);
                        }
                        else
                        {
                            auto wasteCards = m_waste->Flush();
                            m_deck->AddCards(wasteCards);
                        }
                    }
                }
                    break;
                case HitTestZone::PlayArea:
                case HitTestZone::Foundations:
                case HitTestZone::Waste:
                {
                    auto containerHitTestRect = m_zoneRects[zoneType];
                    auto [foundPile, hitTestResult, hitTestZone] = HitTestPiles(point, { Pile::HitTestTarget::Card });
                    if (foundPile)
                    {
                        auto canSplit = foundPile->CanSplit(hitTestResult.CardIndex);
                        auto canTake = foundPile->CanTake(hitTestResult.CardIndex);

                        if (canSplit || canTake)
                        {
                            m_lastPile = foundPile;
                            if (canSplit)
                            {
                                auto [containers, cards, operation] = foundPile->Split(hitTestResult.CardIndex);
                                m_selectedItemContainers = containers;
                                m_selectedCards = cards;
                                m_lastOperation = operation;
                            }
                            else if (canTake)
                            {
                                auto [container, card, operation] = foundPile->Take(hitTestResult.CardIndex);
                                m_selectedItemContainers = { container };
                                m_selectedCards = { card };
                                m_lastOperation = operation;
                            }
                            // The first visual in the list will have its offset updated by 
                            // the pile. However, the pile only knows about its own transforms,
                            // not the container's. Update the offset so the visual shows up
                            // properly when added to the selection layer.
                            m_selectedVisual = m_selectedItemContainers.front().Root;
                            auto offset = m_selectedVisual.Offset();
                            offset.x += containerHitTestRect.X;
                            offset.y += containerHitTestRect.Y;
                            m_selectedVisual.Offset(offset);
                            m_lastHitTest = hitTestResult;
                        }
                    }
                }
                    break;
                default:
                    continue;
                }

                break;
            }
        }

        if (m_selectedVisual)
        {
            m_selectedLayer.Children().InsertAtTop(m_selectedVisual);
            float3 const offset = m_selectedVisual.Offset();
            m_offset.x = offset.x - point.x;
            m_offset.y = offset.y - point.y;
        }
    }

    void OnPointerMoved(IInspectable const &, PointerEventArgs const & args)
    {
        if (m_selectedVisual)
        {
            float2 const point = args.CurrentPoint().Position();

            m_selectedVisual.Offset(
            {
                point.x + m_offset.x,
                point.y + m_offset.y,
                0.0f
            });
        }
    }

    std::tuple<std::shared_ptr<Pile>, Pile::HitTestResult, HitTestZone> HitTestPiles(
        float2 const point, 
        std::initializer_list<Pile::HitTestTarget> const& desiredTargets)
    {   
        
        {
            auto [result, stack] = HitTestPiles(point, m_stacks, HitTestZone::PlayArea, desiredTargets);
            if (result.Target != Pile::HitTestTarget::None)
            {
                return { stack, result, HitTestZone::PlayArea };
            }
        }
        
        {
            auto [result, foundation] = HitTestPiles(point, m_foundations, HitTestZone::Foundations, desiredTargets);
            if (result.Target != Pile::HitTestTarget::None)
            {
                return { foundation, result, HitTestZone::Foundations };
            }
        }

        {
            auto [result, waste] = HitTestPiles<Waste>(point, { m_waste }, HitTestZone::Waste, desiredTargets);
            if (result.Target != Pile::HitTestTarget::None)
            {
                return { waste, result, HitTestZone::Waste };
            }
        }
        
        return { nullptr, Pile::HitTestResult(), HitTestZone::None };
    }

    template <typename PileType>
    std::pair<Pile::HitTestResult, std::shared_ptr<PileType>> HitTestPiles(
        float2 const point, 
        std::vector<std::shared_ptr<PileType>> const& piles,
        HitTestZone zoneType,
        std::initializer_list<Pile::HitTestTarget> const& desiredTargets)
    {
        // The point is in window space
        // Convert from window space to container space (Play Area, Foundation, etc)
        auto containerPoint = point;
        auto containerHitTestRect = m_zoneRects[zoneType];
        containerPoint.x -= containerHitTestRect.X;
        containerPoint.y -= containerHitTestRect.Y;
        for (auto& pile : piles)
        {
            // The point is in container space
            // Convert from container space to local space for the pile
            auto localPoint = containerPoint;
            localPoint.x -= pile->Base().Offset().x;
            localPoint.y -= pile->Base().Offset().y;
            auto result = pile->HitTest(localPoint);
            for (auto& target : desiredTargets)
            {
                if (result.Target == target)
                {
                    return { result, pile };
                }
            }
        }

        return { Pile::HitTestResult(), nullptr };
    }

    fire_and_forget DisplayWinMessage()
    {
        auto dialog = MessageDialog(L"You won!");
        co_await dialog.ShowAsync();
    }

    void OnPointerReleased(IInspectable const&, PointerEventArgs const& args)
    {
        if (m_selectedVisual)
        {
            float2 const point = args.CurrentPoint().Position();
            m_selectedLayer.Children().RemoveAll();

            auto [foundPile, hitTestResult, hitTestZone] = HitTestPiles(point, { Pile::HitTestTarget::Card, Pile::HitTestTarget::Base });
            auto shouldBeInPile = foundPile && foundPile->CanAdd(m_selectedCards);

            for (auto& container : m_selectedItemContainers)
            {
                container.Content.Children().RemoveAll();
            }

            if (shouldBeInPile)
            {
                foundPile->Add(m_selectedCards);

                if (m_lastPile)
                {
                    m_lastPile->CompleteRemoval(m_lastOperation);
                }

                // If we just added something to a foundation, let's check to see
                // if the player has won.
                if (hitTestZone == HitTestZone::Foundations)
                {
                    auto hasWon = true;
                    for (auto& foundation : m_foundations)
                    {
                        auto cards = foundation->Cards();
                        if (cards.size() != 13)
                        {
                            hasWon = false;
                            break;
                        }
                    }

                    if (hasWon)
                    {
                        DisplayWinMessage();
                    }
                }
            }
            else if (m_lastPile)
            {
                m_lastPile->Return(m_selectedCards, m_lastOperation);
            }
            else
            {
                WINRT_ASSERT(false);
            }
        }
        m_selectedVisual = nullptr;
        m_selectedCards.clear();
        m_selectedItemContainers.clear();
        m_lastOperation = Pile::RemovalOperation();
        m_lastPile = nullptr;
        m_lastHitTest = Pile::HitTestResult();
    }

    void App::OnSizeChanged(CoreWindow const& window, WindowSizeChangedEventArgs const& args)
    {
        auto playAreaOffsetY = m_playAreaVisual.Offset().y;
        m_zoneRects.insert({ HitTestZone::PlayArea, { 0, playAreaOffsetY, window.Bounds().Width, window.Bounds().Height - playAreaOffsetY } });
        m_zoneRects[HitTestZone::Foundations] = { window.Bounds().Width - m_foundationVisual.Size().x, 0, m_foundationVisual.Size().x, m_foundationVisual.Size().y };
    }
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    CoreApplication::Run(make<App>());
}
