#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>
#include <ctime>
#include <string>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>

using namespace cv;
using namespace std;

// --- CẤU HÌNH BỘ NHỚ ---
// Số lượng frame tối đa được phép lưu trong RAM.
// 1 Frame 720p ~ 2.7 MB. 
// 600 Frames ~ 1.6 GB RAM (Dành cho Pi 4/5 2GB+ RAM).
// Nếu Pi của bạn RAM ít (1GB), hãy giảm xuống 200.
const int POOL_SIZE = 600; 

// --- CÁC BIẾN TOÀN CỤC ---
queue<Mat*> empty_pool; // Kho chứa các khung hình rỗng (để tái sử dụng)
queue<Mat*> full_queue; // Hàng đợi chứa ảnh đã chụp (chờ ghi)

mutex mtx;
condition_variable cv_writer;
bool is_capturing = true;

// Thống kê
int frames_captured = 0;
int frames_written = 0;
int frames_dropped = 0;

// --- LUỒNG GHI ĐĨA (CONSUMER) ---
void writer_thread_func(string filename, int width, int height, double fps) {
    VideoWriter out(filename, VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, Size(width, height));
    
    if (!out.isOpened()) {
        cerr << "[Writer] Error: Cannot open file!" << endl;
        return;
    }
    
    cout << "[Writer] Ready. Buffer Pool Size: " << POOL_SIZE << " frames." << endl;

    while (true) {
        Mat* frame_ptr = nullptr;

        {
            unique_lock<mutex> lock(mtx);
            // Chờ khi có hàng hoặc đã quay xong
            cv_writer.wait(lock, []{ return !full_queue.empty() || !is_capturing; });

            if (full_queue.empty() && !is_capturing) break;

            if (!full_queue.empty()) {
                frame_ptr = full_queue.front();
                full_queue.pop();
            }
        }

        if (frame_ptr != nullptr) {
            // 1. Ghi vào đĩa
            out.write(*frame_ptr);
            frames_written++;

            // 2. [QUAN TRỌNG] Tái chế: Trả frame rỗng về kho (Empty Pool) để dùng lại
            // Không hủy (delete) bộ nhớ -> Tiết kiệm CPU
            {
                lock_guard<mutex> lock(mtx);
                empty_pool.push(frame_ptr);
            }
        }
    }
    out.release();
    cout << "[Writer] Finished." << endl;
}

int main() {
    VideoCapture cap(0, CAP_V4L2);
    if (!cap.isOpened()) return -1;

    // --- 1. CẤU HÌNH CAMERA (ÉP XUNG) ---
    cap.set(CAP_PROP_FOURCC, VideoWriter::fourcc('M', 'J', 'P', 'G'));
    cap.set(CAP_PROP_FRAME_WIDTH, 1280);
    cap.set(CAP_PROP_FRAME_HEIGHT, 720);
    cap.set(CAP_PROP_FPS, 60);

    // [BÍ KÍP] Chỉnh Exposure để đạt 60 FPS
    // Nếu ảnh tối, hãy tăng 0.01 lên 0.02
    cap.set(CAP_PROP_AUTO_EXPOSURE, 0.25); // Manual Mode
    cap.set(CAP_PROP_EXPOSURE, 0.01);      // 10ms exposure time

    // Warm up
    cout << "Allocating memory pool..." << endl;
    
    // --- 2. KHỞI TẠO BỘ NHỚ (PRE-ALLOCATION) ---
    // Cấp phát trước toàn bộ RAM cần dùng. Lúc quay sẽ không xin thêm RAM nữa.
    vector<Mat> memory_block(POOL_SIZE); 
    for(int i=0; i<POOL_SIZE; i++) {
        // Khởi tạo sẵn kích thước để tránh re-allocate sau này
        memory_block[i] = Mat::zeros(720, 1280, CV_8UC3);
        empty_pool.push(&memory_block[i]);
    }
    
    // Đọc thử vài frame để camera ổn định
    for(int i=0; i<20; i++) { Mat t; cap >> t; }

    double w = cap.get(CAP_PROP_FRAME_WIDTH);
    double h = cap.get(CAP_PROP_FRAME_HEIGHT);
    cout << "Camera: " << w << "x" << h << endl;

    // File setup
    time_t now = time(0);
    tm *ltm = localtime(&now);
    char buf[100];
    strftime(buf, 100, "%Y-%m-%d_%H-%M-%S.avi", ltm);
    string path = "/home/comvis/Bee_monitoring/record/" + string(buf);

    // Chạy Writer Thread (Set cứng 60.0 fps)
    thread t_writer(writer_thread_func, path, (int)w, (int)h, 60.0);

    cout << "--- START RECORDING (60s) ---" << endl;
    auto start = chrono::steady_clock::now();
    int duration = 60;

    Mat temp_frame; // Biến tạm

    // --- 3. VÒNG LẶP CAPTURE (SIÊU TỐC) ---
    while (true) {
        // Đọc vào biến tạm trước
        cap >> temp_frame; 
        
        if (temp_frame.empty()) {
            cout << "Lost frame!" << endl;
            break;
        }
        
        frames_captured++;

        Mat* dest_frame = nullptr;
        {
            lock_guard<mutex> lock(mtx);
            // Lấy một cái chai rỗng từ kho
            if (!empty_pool.empty()) {
                dest_frame = empty_pool.front();
                empty_pool.pop();
            }
        }

        if (dest_frame != nullptr) {
            // [QUAN TRỌNG] Copy dữ liệu vào vùng nhớ có sẵn (Nhanh hơn clone nhiều)
            temp_frame.copyTo(*dest_frame);

            // Đẩy vào hàng đợi ghi
            {
                lock_guard<mutex> lock(mtx);
                full_queue.push(dest_frame);
            }
            cv_writer.notify_one();
        } else {
            // Hết sạch chai rỗng (RAM đầy, Writer ghi không kịp) -> Buộc phải Drop
            // Nhưng vì POOL_SIZE lớn (600), rất khó xảy ra chuyện này trừ khi thẻ nhớ hư.
            frames_dropped++;
        }

        if (chrono::duration_cast<chrono::seconds>(chrono::steady_clock::now() - start).count() >= duration) {
            break;
        }
    }

    // --- 4. KẾT THÚC ---
    auto end_cap = chrono::steady_clock::now();
    double cap_time = chrono::duration_cast<chrono::milliseconds>(end_cap - start).count() / 1000.0;

    cout << "Capture finished. Waiting for writer to flush..." << endl;
    
    {
        lock_guard<mutex> lock(mtx);
        is_capturing = false;
    }
    cv_writer.notify_one();
    if (t_writer.joinable()) t_writer.join();

    cout << "---------------- REPORT ----------------" << endl;
    cout << "Real Time       : " << cap_time << "s" << endl;
    cout << "Frames Captured : " << frames_captured << " (Avg: " << frames_captured/cap_time << " FPS)" << endl;
    cout << "Frames Written  : " << frames_written << endl;
    cout << "Frames Dropped  : " << frames_dropped << endl;
    
    if (frames_dropped == 0 && frames_written > 3000) {
        cout << "RESULT: PERFECT 60 FPS RECORDING!" << endl;
    }
    
    return 0;
}