#include <opencv2/opencv.hpp>
#include <iostream>
#include <fstream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <iomanip>
#include <algorithm>

using namespace cv;
using namespace std;

const int TARGET_FPS = 60;
const int RECORD_SECONDS = 60;
const string RAM_DIR = "/dev/shm/";
const string SD_DIR = "/home/comvis/Bee_monitoring/record/h264/";

queue<Mat> frame_queue;
mutex mtx;
condition_variable cv_writer;
bool is_capturing = true;
int frames_written = 0;

string getCmdOption(char ** begin, char ** end, const std::string & option) {
    char ** itr = std::find(begin, end, option);
    if (itr != end && ++itr != end) {
        return std::string(*itr);
    }
    return "";
}

string get_timestamp_str() {
    time_t now = time(0);
    tm *ltm = localtime(&now);
    char buf[100];
    strftime(buf, 100, "%Y-%m-%d_%H-%M-%S", ltm);
    return string(buf);
}

void writer_thread_func(string filename) {
    ofstream outfile(filename, ios::out | ios::binary);
    if (!outfile.is_open()) {
        cerr << "[Writer] Critical Error: Cannot create file in RAM: " << filename << endl;
        return;
    }
    
    cout << "[Writer] Thread Ready. Writing to RAM: " << filename << endl;

    while (true) {
        Mat current_frame;
        {
            unique_lock<mutex> lock(mtx);
            cv_writer.wait(lock, []{ return !frame_queue.empty() || !is_capturing; });
            
            if (frame_queue.empty() && !is_capturing) break;
            
            if (!frame_queue.empty()) {
                current_frame = frame_queue.front();
                frame_queue.pop();
            }
        }

        if (!current_frame.empty()) {
            outfile.write((char*)current_frame.data, current_frame.total() * current_frame.elemSize());
            frames_written++;
        }
    }
    outfile.close();
    cout << "[Writer] Recording stopped. RAM buffer closed." << endl;
}

int main(int argc, char** argv) {
    system(("mkdir -p " + SD_DIR).c_str());
    string input_name = getCmdOption(argv, argv + argc, "--name");
    string filename_base = "";

    if (!input_name.empty()) {
        size_t last_slash = input_name.find_last_of("/\\");
        string basename = (last_slash != string::npos) ? input_name.substr(last_slash + 1) : input_name;

        size_t last_dot = basename.find_last_of(".");
        if (last_dot != string::npos) {
            filename_base = basename.substr(0, last_dot);
        } else {
            filename_base = basename;
        }
    } else {
        filename_base = "pi03_" + get_timestamp_str();
    }

    string ram_path = RAM_DIR + filename_base + ".mjpg";
    string sd_path  = SD_DIR + filename_base + ".mp4";

    cout << "--- PATH CONFIG ---" << endl;
    cout << "Final MP4: " << sd_path << endl;
    cout << "Temp RAM : " << ram_path << endl;

    cout << "[Camera] Forcing Exposure Settings..." << endl;
    system("v4l2-ctl -d /dev/video0 -c exposure_auto=1");
    system("v4l2-ctl -d /dev/video0 -c exposure_absolute=50"); // Chỉnh số này nếu ảnh quá tối/sáng

    VideoCapture cap(0, CAP_V4L2);
    if (!cap.isOpened()) {
        cerr << "[Error] Cannot open camera /dev/video0" << endl;
        return -1;
    }

    cap.set(CAP_PROP_FOURCC, VideoWriter::fourcc('M', 'J', 'P', 'G'));

    cap.set(CAP_PROP_FRAME_WIDTH, 1280);
    cap.set(CAP_PROP_FRAME_HEIGHT, 720);
    cap.set(CAP_PROP_FPS, TARGET_FPS);
    cap.set(CAP_PROP_CONVERT_RGB, 0); 

    double fps_set = cap.get(CAP_PROP_FPS);
    cout << "[Camera] Initialized. Driver reports FPS: " << fps_set << endl;

    thread t_writer(writer_thread_func, ram_path);

    cout << "--- START RECORDING (60s) ---" << endl;
    
    auto start_time = chrono::steady_clock::now();
    auto last_log = start_time;
    int frames_read = 0;
    int frames_in_sec = 0;

    Mat raw_frame;
    
    while (true) {
        if (cap.read(raw_frame)) {
            frames_read++;
            frames_in_sec++;
            {
                lock_guard<mutex> lock(mtx);
                frame_queue.push(raw_frame.clone()); 
            }
            cv_writer.notify_one();
        } else {
            cout << "[Error] Frame dropped or Camera disconnected!" << endl;
            break;
        }

        auto now = chrono::steady_clock::now();
        double elapsed_sec = chrono::duration_cast<chrono::duration<double>>(now - last_log).count();
        if (elapsed_sec >= 1.0) {
            cout << "\r[Running] FPS: " << fixed << setprecision(1) << (frames_in_sec / elapsed_sec) 
                 << " | Queue: " << frame_queue.size() << "    " << flush;
            frames_in_sec = 0;
            last_log = now;
        }
        double total_elapsed = chrono::duration_cast<chrono::duration<double>>(now - start_time).count();
        if (total_elapsed >= RECORD_SECONDS) break;
    }
    {
        lock_guard<mutex> lock(mtx);
        is_capturing = false;
    }
    cv_writer.notify_all();
    
    if (t_writer.joinable()) t_writer.join();
    cap.release();

    auto end_time = chrono::steady_clock::now();
    double final_duration = chrono::duration_cast<chrono::duration<double>>(end_time - start_time).count();
    double avg_fps = frames_written / final_duration;

    cout << "\n---------------- REPORT ----------------" << endl;
    cout << "Duration     : " << final_duration << "s" << endl;
    cout << "Frames Valid : " << frames_written << endl;
    cout << "Average FPS  : " << avg_fps << endl;

    if (frames_written < 100) {
        cerr << "[Warning] Frame count too low! Check Camera Light/Exposure." << endl;
    } else {
        cout << "[FFmpeg] Encoding from RAM to SD Card..." << endl;
        string cmd = "ffmpeg -y -framerate " + to_string(avg_fps) + " -f mjpeg -i " + ram_path + 
                     " -c:v libx264 -preset fast -pix_fmt yuv420p " + 
                     sd_path + " > /dev/null 2>&1";
        
        system(cmd.c_str());
        remove(ram_path.c_str());
        cout << "[Success] Video Saved: " << sd_path << endl;
    }

    return 0;
}