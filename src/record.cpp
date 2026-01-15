#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>
#include <ctime>
#include <string>

using namespace cv;
using namespace std;

// Hàm lấy tên file theo thời gian
string get_timestamp_filename() {
    time_t now = time(0);
    tm *ltm = localtime(&now);
    char buf[100];
    strftime(buf, 100, "pi03_%Y-%m-%d_%H-%M-%S.mjpg", ltm); // Lưu đuôi .mjpg
    return string(buf);
}

int main(int argc, char** argv) {
    // 1. CHUẨN BỊ ĐƯỜNG DẪN
    string save_path = "/home/comvis/Bee_monitoring/record/" + get_timestamp_filename();
    cout << "Recording to: " << save_path << endl;

    // 2. XÂY DỰNG PIPELINE GSTREAMER
    // Giải thích Pipeline:
    // v4l2src: Lấy dữ liệu từ camera (Linux)
    // tee: Chia dòng dữ liệu làm 2 nhánh (t.)
    // Nhánh 1 (Save): queue -> filesink (Ghi thẳng xuống đĩa, cực nhanh)
    // Nhánh 2 (App):  queue -> jpegdec (giải mã) -> videoconvert -> appsink (đưa vào OpenCV Mat)
    
    string pipeline = "v4l2src device=/dev/video0 ! "
                      "image/jpeg, width=1280, height=720, framerate=60/1 ! "
                      "tee name=t "
                      "t. ! queue max-size-buffers=0 max-size-time=0 ! "
                      "filesink location=" + save_path + " "
                      "t. ! queue max-size-buffers=1 ! "
                      "jpegdec ! videoconvert ! appsink sync=false";

    cout << "Opening Pipeline..." << endl;
    
    // Mở Camera bằng backend GStreamer
    VideoCapture cap(pipeline, CAP_GSTREAMER);

    if (!cap.isOpened()) {
        cerr << "Error: Could not open GStreamer pipeline. Check v4l2-ctl settings!" << endl;
        return -1;
    }

    cout << "--- START RECORDING (GStreamer Pipeline) ---" << endl;
    
    Mat frame;
    int frames_read = 0;
    auto start_time = chrono::steady_clock::now();

    while (true) {
        // Đọc frame từ nhánh 'appsink'. 
        // Lưu ý: Việc GHI FILE đang diễn ra ngầm ở nhánh 'filesink' của GStreamer
        // nên việc gọi cap.read() chậm hay nhanh không ảnh hưởng việc mất frame của file ghi.
        if (cap.read(frame)) {
            frames_read++;
            
            // --- XỬ LÝ FRAME TẠI ĐÂY (NẾU CẦN) ---
            // Ví dụ: imshow("Monitor", frame);
            // waitKey(1);
            // -------------------------------------
        } else {
            cout << "Stream ended or error." << endl;
            break;
        }

        // Kiểm tra 60s
        auto current_time = chrono::steady_clock::now();
        double elapsed = chrono::duration_cast<chrono::duration<double>>(current_time - start_time).count();
        if (elapsed >= 60.0) break;
    }

    cap.release(); // Quan trọng: Đóng pipeline để flush dữ liệu xuống file
    
    cout << "Finished. Total frames read by App: " << frames_read << endl;
    
    // Convert sang MP4 sau khi quay xong (Offline processing)
    string mp4_path = save_path + ".mp4"; // output name trick
    cout << "Converting to MP4..." << endl;
    string cmd = "ffmpeg -y -f mjpeg -framerate 60 -i " + save_path + 
                 " -c:v libx264 -preset ultrafast -pix_fmt yuv420p " + 
                 "/home/comvis/Bee_monitoring/record/h264/" + get_timestamp_filename() + ".mp4";
    system(cmd.c_str());

    return 0;
}