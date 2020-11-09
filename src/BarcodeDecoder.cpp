#include "BarcodeDecoder.h"

#include <QDebug>
#include <QImage>
#include <QtMultimedia/qvideoframe.h>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <iostream>

#include <ReadBarcode.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace ZXing {
namespace Qt {

using ZXing::DecodeHints;
using ZXing::BarcodeFormat;
using ZXing::BarcodeFormats;
using ZXing::Binarizer;

template <typename T, typename _ = decltype(ToString(T()))>
QDebug operator<<(QDebug dbg, const T& v)
{
    return dbg.noquote() << QString::fromStdString(ToString(v));
}

class Result : private ZXing::Result
{
public:
    explicit Result(ZXing::Result&& r) : ZXing::Result(std::move(r)) {}

    using ZXing::Result::format;
    using ZXing::Result::isValid;
    using ZXing::Result::status;

    inline QString text() const { return QString::fromWCharArray(ZXing::Result::text().c_str()); }
};

Result ReadBarcode(const QImage& img, const DecodeHints& hints = {})
{
    auto ImgFmtFromQImg = [](const QImage& img) {
        switch (img.format()) {
        case QImage::Format_ARGB32:
        case QImage::Format_RGB32:
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
            return ImageFormat::BGRX;
#else
            return ImageFormat::XRGB;
#endif
        case QImage::Format_RGB888: return ImageFormat::RGB;
        case QImage::Format_RGBX8888:
        case QImage::Format_RGBA8888: return ImageFormat::RGBX;
        case QImage::Format_Grayscale8: return ImageFormat::Lum;
        default: return ImageFormat::None;
        }
    };

    auto exec = [&](const QImage& img) {
        return Result(ZXing::ReadBarcode({img.bits(), img.width(), img.height(), ImgFmtFromQImg(img)}, hints));
    };

    return ImgFmtFromQImg(img) == ImageFormat::None ? exec(img.convertToFormat(QImage::Format_RGBX8888)) : exec(img);
}

} // Qt namespace
} // ZXing namespace

using namespace ZXing::Qt;

std::ostream& operator<<(std::ostream& os, const std::vector<ZXing::ResultPoint>& points) {
    for (const auto& p : points)
        os << int(p.x() + .5f) << "x" << int(p.y() + .5f) << " ";
    return os;
}

BarcodeDecoder::BarcodeDecoder(QObject *parent) : QObject(parent)
{

}

void BarcodeDecoder::clean()
{
    _captured = "";
}

QString BarcodeDecoder::captured() const
{
    return _captured;
}

void BarcodeDecoder::setCaptured(const QString &captured)
{
    if (_captured == captured) {
        return;
    }

    _captured = captured;
    emit capturedChanged(_captured);
}

void BarcodeDecoder::setIsDecoding(bool isDecoding)
{
    if (_isDecoding == isDecoding) {
        return;
    }

    _isDecoding = isDecoding;
    emit isDecodingChanged(_isDecoding);
}

bool BarcodeDecoder::isDecoding() const
{
    return _isDecoding;
}

void BarcodeDecoder::process(const QImage capturedImage)
{
    setIsDecoding(true);

    const auto hints = DecodeHints()
            .setFormats(BarcodeFormat::QR_CODE
                        | BarcodeFormat::DATA_MATRIX
                        | BarcodeFormat::CODE_128
                        | BarcodeFormat::CODABAR)
            .setTryHarder(true)
            .setTryRotate(true)
            .setIsPure(false)
            .setBinarizer(Binarizer::LocalAverage);

    const auto result = ReadBarcode(capturedImage, hints);

    if (result.isValid()) {
       setCaptured(result.text());
    }

    setIsDecoding(false);
}

QImage BarcodeDecoder::videoFrameToImage(const QVideoFrame &videoFrame, const QRect &captureRect)
{
    if (videoFrame.handleType() == QAbstractVideoBuffer::NoHandle) {

        QImage image = videoFrame.image();

        if (image.isNull()) {
            return QImage();
        }

        if ( image.format() != QImage::Format_ARGB32) {
            image = image.convertToFormat(QImage::Format_ARGB32);
        }

        return image.copy(captureRect);
    }

    if (videoFrame.handleType() == QAbstractVideoBuffer::GLTextureHandle) {
        QImage image(videoFrame.width(), videoFrame.height(), QImage::Format_ARGB32);
        GLuint textureId = static_cast<GLuint>(videoFrame.handle().toInt());
        QOpenGLContext* ctx = QOpenGLContext::currentContext();
        QOpenGLFunctions* f = ctx->functions();
        GLuint fbo;
        f->glGenFramebuffers( 1, &fbo);
        GLint prevFbo;
        f->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId, 0);
        f->glReadPixels(0, 0,  videoFrame.width(),  videoFrame.height(), GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
        f->glBindFramebuffer( GL_FRAMEBUFFER, static_cast<GLuint>( prevFbo ) );
        return image.rgbSwapped().copy(captureRect);
    }

    return QImage();
}
