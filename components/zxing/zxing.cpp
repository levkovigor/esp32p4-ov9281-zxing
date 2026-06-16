#include "zxing.h"
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include <ReadBarcode.h>

extern "C" {
    #include "esp_log.h"
}

static const char *TAG = "zxing_component";

void func()
{
    ESP_LOGW(TAG, "buffer in component `zxing`");
}

void process_frame(uint8_t * dataFrame, int dataFrameSize, int width, int height)
{
    if(!dataFrame)
    {
        ESP_LOGE(TAG, "dataframe is empty");
        return;
    }

    // ZXing configuration - STRICTLY ENABLE ROTATION
    auto hints = ZXing::DecodeHints()
                   .setFormats(ZXing::BarcodeFormat::QRCode | ZXing::BarcodeFormat::Code128 | ZXing::BarcodeFormat::EAN13 | ZXing::BarcodeFormat::UPCA)
                   .setTryRotate(true)
                   .setTryHarder(false);

    // Create image view (Grayscale/Lum)
    ZXing::ImageView image(dataFrame, width, height, ZXing::ImageFormat::Lum);
    
    static int scan_count = 0;
    if (scan_count++ % 50 == 0) {
        ESP_LOGI(TAG, "Scanning active (320x200)...");
    }

    auto results = ZXing::ReadBarcodes(image, hints);

    if (!results.empty()) {
        for (const auto& r : results) {
            auto pos = r.position();
            
            // Scaling back to 640x400 (since input is 320x200)
            float scale = 2.0f;
            
            // ZXing Points index: 0:TopLeft, 1:BottomLeft, 2:BottomRight, 3:TopRight
            float x0 = pos[0].x * scale; float y0 = pos[0].y * scale;
            float x1 = pos[1].x * scale; float y1 = pos[1].y * scale;
            float x2 = pos[2].x * scale; float y2 = pos[2].y * scale;
            float x3 = pos[3].x * scale; float y3 = pos[3].y * scale;

            // Distances (distX, distY) - average of opposite sides
            float d_top = sqrtf(powf(x3 - x0, 2) + powf(y3 - y0, 2));
            float d_bottom = sqrtf(powf(x2 - x1, 2) + powf(y2 - y1, 2));
            float d_left = sqrtf(powf(x0 - x1, 2) + powf(y0 - y1, 2));
            float d_right = sqrtf(powf(x3 - x2, 2) + powf(y3 - y2, 2));
            
            float distX = (d_top + d_bottom) / 2.0f;
            float distY = (d_left + d_right) / 2.0f;

            // Center and Deltas relative to 640x400 center (320, 200)
            float centerX = (x0 + x1 + x2 + x3) / 4.0f;
            float centerY = (y0 + y1 + y2 + y3) / 4.0f;
            float deltaX = centerX - 320.0f;
            float deltaY = centerY - 200.0f;

            // Angle (degrees) - slope of the top edge (TopLeft to TopRight)
            float angle = atan2f(y3 - y0, x3 - x0) * 180.0f / 3.1415926535f;

            // Final output format: deltaX:deltaY:angle:distX:distY:data
            printf("%.2f:%.2f:%.2f:%.2f:%.2f:%s\n", 
                   deltaX, deltaY, angle, distX, distY, r.text().c_str());
        }
    }
}