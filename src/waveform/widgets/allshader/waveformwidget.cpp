
#include "waveform/widgets/allshader/waveformwidget.h"

#include <QApplication>
#include <QWheelEvent>

#include "waveform/renderers/allshader/waveformrenderbackground.h"
#include "waveform/renderers/allshader/waveformrenderbeat.h"
#include "waveform/renderers/allshader/waveformrendererendoftrack.h"
#include "waveform/renderers/allshader/waveformrendererfiltered.h"
#include "waveform/renderers/allshader/waveformrendererhsv.h"
#include "waveform/renderers/allshader/waveformrendererpreroll.h"
#include "waveform/renderers/allshader/waveformrendererrgb.h"
#include "waveform/renderers/allshader/waveformrenderersimple.h"
#include "waveform/renderers/allshader/waveformrendererslipmode.h"
#include "waveform/renderers/allshader/waveformrendererstem.h"
#include "waveform/renderers/allshader/waveformrenderertextured.h"
#include "waveform/renderers/allshader/waveformrendermark.h"
#include "waveform/renderers/allshader/waveformrendermarkrange.h"
#include "waveform/widgets/allshader/moc_waveformwidget.cpp"

namespace allshader {

WaveformWidget::WaveformWidget(QWidget* parent,
        WaveformWidgetType::Type type,
        const QString& group,
        WaveformRendererSignalBase::Options options)
        : WGLWidget(parent), WaveformWidgetAbstract(group) {
    auto pTopNode = std::make_unique<rendergraph::Node>();
    auto pOpacityNode = std::make_unique<rendergraph::OpacityNode>();

    pTopNode->appendChildNode(addRendererNode<WaveformRenderBackground>());
    pOpacityNode->appendChildNode(addRendererNode<WaveformRendererEndOfTrack>());
    pOpacityNode->appendChildNode(addRendererNode<WaveformRendererPreroll>());
    pOpacityNode->appendChildNode(addRendererNode<WaveformRenderMarkRange>());
    m_pWaveformRenderMarkRange = static_cast<WaveformRenderMarkRange*>(pOpacityNode->lastChild());

#ifdef __STEM__
    // The following two renderers work in tandem: if the rendered waveform is
    // for a stem track, WaveformRendererSignalBase will skip rendering and let
    // WaveformRendererStem do the rendering, and vice-versa.
    pOpacityNode->appendChildNode(addRendererNode<WaveformRendererStem>());
#endif
    pOpacityNode->appendChildNode(addWaveformSignalRendererNode(
            type, options, ::WaveformRendererAbstract::Play));
    pOpacityNode->appendChildNode(addRendererNode<WaveformRenderBeat>());
    pOpacityNode->appendChildNode(addRendererNode<WaveformRenderMark>());
    m_pWaveformRenderMark = static_cast<WaveformRenderMark*>(pOpacityNode->lastChild());

    // if the added signal renderer supports slip, we add it again, now for
    // slip, together with the other slip renderers
    if (m_pWaveformRendererSignal && m_pWaveformRendererSignal->supportsSlip()) {
        // The following renderer will add an overlay waveform if a slip is in progress
        pOpacityNode->appendChildNode(addRendererNode<WaveformRendererSlipMode>());
        pOpacityNode->appendChildNode(
                addRendererNode<WaveformRendererPreroll>(
                        ::WaveformRendererAbstract::Slip));
#ifdef __STEM__
        pOpacityNode->appendChildNode(
                addRendererNode<WaveformRendererStem>(
                        ::WaveformRendererAbstract::Slip));
#endif
        pOpacityNode->appendChildNode(addWaveformSignalRendererNode(
                type, options, ::WaveformRendererAbstract::Slip));
        pOpacityNode->appendChildNode(
                addRendererNode<WaveformRenderBeat>(
                        ::WaveformRendererAbstract::Slip));
        pOpacityNode->appendChildNode(
                addRendererNode<WaveformRenderMark>(
                        ::WaveformRendererAbstract::Slip));
    }

    m_initSuccess = init();

    pTopNode->appendChildNode(std::move(pOpacityNode));
    m_pOpacityNode = static_cast<rendergraph::OpacityNode*>(pTopNode->lastChild());

    m_pEngine = std::make_unique<rendergraph::Engine>(std::move(pTopNode));
}

WaveformWidget::~WaveformWidget() {
    makeCurrentIfNeeded();
    m_rendererStack.clear();
    m_pEngine.reset();
    doneCurrent();
}

std::unique_ptr<rendergraph::BaseNode>
WaveformWidget::addWaveformSignalRendererNode(WaveformWidgetType::Type type,
        WaveformRendererSignalBase::Options options,
        ::WaveformRendererAbstract::PositionSource positionSource) {
#ifndef QT_OPENGL_ES_2
    if (options & allshader::WaveformRendererSignalBase::Option::HighDetail) {
        switch (type) {
        case ::WaveformWidgetType::RGB:
        case ::WaveformWidgetType::Filtered:
        case ::WaveformWidgetType::Stacked:
            return addWaveformSignalRendererNode<WaveformRendererTextured>(
                    type, positionSource, options);
        default:
            break;
        }
    }
#endif

    switch (type) {
    case ::WaveformWidgetType::Simple:
        return addWaveformSignalRendererNode<WaveformRendererSimple>();
    case ::WaveformWidgetType::RGB:
        return addWaveformSignalRendererNode<WaveformRendererRGB>(positionSource, options);
    case ::WaveformWidgetType::HSV:
        return addWaveformSignalRendererNode<WaveformRendererHSV>();
    case ::WaveformWidgetType::Filtered:
        return addWaveformSignalRendererNode<WaveformRendererFiltered>(false);
    case ::WaveformWidgetType::Stacked:
        return addWaveformSignalRendererNode<WaveformRendererFiltered>(
                true); // true for RGB Stacked
    default:
        break;
    }
    return nullptr;
}

mixxx::Duration WaveformWidget::render() {
    makeCurrentIfNeeded();
    paintGL();
    doneCurrent();
    // In the legacy widgets, this is used to "return timer for painter setup"
    // which is not relevant here. Also note that the return value is not used
    // at all, so it might be better to remove it everywhere. In the meantime.
    // we need to return something for API compatibility.
    return mixxx::Duration();
}

void WaveformWidget::paintGL() {
    // opacity of 0.f effectively skips the subtree rendering
    m_pOpacityNode->setOpacity(shouldOnlyDrawBackground() ? 0.f : 1.f);

    m_pEngine->preprocess();
    m_pEngine->render();
}

void WaveformWidget::castToQWidget() {
    m_widget = this;
}

void WaveformWidget::initializeGL() {
}

void WaveformWidget::resizeRenderer(int, int, float) {
    // defer to resizeGL
}

void WaveformWidget::resizeGL(int w, int h) {
    w = static_cast<int>(std::lround(static_cast<qreal>(w) / devicePixelRatioF()));
    h = static_cast<int>(std::lround(static_cast<qreal>(h) / devicePixelRatioF()));

    m_pEngine->resize(w, h);
    WaveformWidgetRenderer::resizeRenderer(w, h, static_cast<float>(devicePixelRatio()));
}

void WaveformWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
}

void WaveformWidget::wheelEvent(QWheelEvent* pEvent) {
    QApplication::sendEvent(parentWidget(), pEvent);
    pEvent->accept();
}

void WaveformWidget::leaveEvent(QEvent* pEvent) {
    QApplication::sendEvent(parentWidget(), pEvent);
    pEvent->accept();
}

/* static */
WaveformRendererSignalBase::Options WaveformWidget::supportedOptions(
        WaveformWidgetType::Type type) {
    WaveformRendererSignalBase::Options options = WaveformRendererSignalBase::Option::None;
    switch (type) {
    case WaveformWidgetType::Type::RGB:
        options = WaveformRendererSignalBase::Option::AllOptionsCombined;
        break;
    case WaveformWidgetType::Type::Filtered:
        options = WaveformRendererSignalBase::Option::HighDetail;
        break;
    case WaveformWidgetType::Type::Stacked:
        options = WaveformRendererSignalBase::Option::HighDetail;
        break;
    default:
        break;
    }
#ifdef QT_OPENGL_ES_2
    // High detail (textured) waveforms are not supported on OpenGL ES.
    // See https://github.com/mixxxdj/mixxx/issues/13385
    options &= ~WaveformRendererSignalBase::Options(WaveformRendererSignalBase::Option::HighDetail);
#endif
    return options;
}

/* static */
WaveformWidgetVars WaveformWidget::vars() {
    WaveformWidgetVars result;
    result.m_useGL = true;
    result.m_useGLES = true;
    result.m_useGLSL = true;
    result.m_category = WaveformWidgetCategory::AllShader;
    return result;
}

} // namespace allshader
