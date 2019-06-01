#include "pch.h"
#include "Card.h"
#include "Pack.h"
#include "ShapeCache.h"

using namespace winrt;

using namespace Windows;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI;
using namespace Windows::UI::Core;
using namespace Windows::UI::Composition;

ShapeVisual BuildCard(
    std::shared_ptr<ShapeCache> const& shapeCache,
    hstring const& card,
    Color const& color)
{
    auto compositor = shapeCache->Compositor();
    auto shapeVisual = compositor.CreateShapeVisual();
    auto shapeContainer = compositor.CreateContainerShape();
    shapeVisual.Shapes().Append(shapeContainer);
    shapeVisual.Size({ 175, 250 });

    auto roundedRectGeometry = compositor.CreateRoundedRectangleGeometry();
    roundedRectGeometry.CornerRadius({ 10, 10 });
    roundedRectGeometry.Size({ 175, 250 });
    auto rectShape = compositor.CreateSpriteShape(roundedRectGeometry);
    rectShape.StrokeBrush(compositor.CreateColorBrush(Colors::Gray()));
    rectShape.FillBrush(compositor.CreateColorBrush(Colors::White()));
    rectShape.StrokeThickness(2);
    shapeContainer.Shapes().Append(rectShape);

    auto pathGeometry = shapeCache->GetPathGeometry(card);
    auto pathShape = compositor.CreateSpriteShape(pathGeometry);
    pathShape.Offset({ 5, 0 });
    pathShape.FillBrush(compositor.CreateColorBrush(color));
    shapeContainer.Shapes().Append(pathShape);

    return shapeVisual;
}

CompositionCard::CompositionCard(
    Card card,
    std::shared_ptr<ShapeCache> const& shapeCache)
{
    m_card = card;
    m_root = BuildCard(
        shapeCache,
        card.ToString(),
        card.IsRed() ? Colors::Crimson() : Colors::Black());
}

Pack::Pack(std::shared_ptr<ShapeCache> const& shapeCache)
{
    m_shapeCache = shapeCache;

    for (auto i = 0; i < (int)Face::King; i++)
    {
        auto face = (Face)(i + 1);
        for (auto j = 0; j < (int)Suit::Club + 1; j++)
        {
            auto suit = (Suit)(j);
            auto card = Card(face, suit);
            auto compositionCard = CompositionCard(card, m_shapeCache);
            m_cards.push_back(compositionCard);
        }
    }
}