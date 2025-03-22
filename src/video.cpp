#include <bitset>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <random>
#include <string>
#include <vector>

using namespace cv;
using namespace std;

std::string original_path;
std::string stego_path;

// ====================== Common Functions ======================
vector<bool> messageToBits(const string &message) {
  vector<bool> bits;
  for (char c : message)
    for (int i = 7; i >= 0; --i)
      bits.push_back((c >> i) & 1);
  return bits;
}

// Encrypts plaintext using Vigenere cipher with a key
std::string vigenereEncrypt(const std::string &plaintext,
                            const std::string &key) {
  std::string ciphertext = "";
  int keyLen = key.length();
  for (size_t i = 0; i < plaintext.size(); ++i) {
    char plainChar = plaintext[i];
    char keyChar = key[i % keyLen];
    // Simple byte shift, not just alphabet
    unsigned char encryptedChar = (plainChar + keyChar) % 256;
    ciphertext += encryptedChar;
  }
  return ciphertext;
}

// Decrypts ciphertext using Vigenere cipher with a key
std::string vigenereDecrypt(const std::string &ciphertext,
                            const std::string &key) {
  std::string plaintext = "";
  int keyLen = key.length();
  for (size_t i = 0; i < ciphertext.size(); ++i) {
    char cipherChar = ciphertext[i];
    char keyChar = key[i % keyLen];
    unsigned char decryptedChar = (cipherChar - keyChar + 256) % 256;
    plaintext += decryptedChar;
  }
  return plaintext;
}

void embedMetadata(cv::Mat &frame, uint8_t metadata) {
  if (frame.empty()) {
    std::cerr << "Frame is empty!\n";
    return;
  }

  if (frame.cols < 8) {
    std::cerr << "Frame not wide enough to embed metadata!\n";
    return;
  }

  for (int p = 0; p < 8; p++) {
    cv::Vec3b &pixel = frame.at<cv::Vec3b>(0, p);

    // Extract the relevant bit from metadata (little-endian)
    uint8_t bit = (metadata >> p) & 1;

    // Clear LSB of the blue channel, then set it
    pixel[0] = (pixel[0] & 0xFE) | bit;
  }

  std::cout << "[*] Embedded metadata: " << (int)metadata << "\n";
}

string bitsToMessage(const vector<bool> &bits) {
  string message;
  for (size_t i = 0; i < bits.size(); i += 8) {
    char c = 0;
    for (int j = 0; j < 8; ++j)
      if (i + j < bits.size()) // Safety check
        c |= bits[i + j] << (7 - j);
    message += c;
  }
  return message;
}

vector<bool> getLengthBits(uint32_t lengthInBits) {
  vector<bool> lengthBits;
  for (int i = 31; i >= 0; --i)
    lengthBits.push_back((lengthInBits >> i) & 1);
  return lengthBits;
}

// ====================== Embed Functions ======================
void embedBitsInFrame(Mat &frame, const vector<bool> &bits, int &bitIndex,
                      bool randomPixels, mt19937 &rng) {
  int rows = frame.rows;
  int cols = frame.cols;
  int channels = frame.channels();

  vector<pair<int, int>> pixelIndices;
  for (int y = 0; y < rows; ++y)
    for (int x = 0; x < cols; ++x)
      pixelIndices.push_back({y, x});

  if (randomPixels)
    shuffle(pixelIndices.begin(), pixelIndices.end(), rng);

  for (auto [y, x] : pixelIndices) {
    Vec3b &pixel = frame.at<Vec3b>(y, x);
    for (int c = 0; c < channels; ++c) {
      if (bitIndex >= bits.size())
        return;
      pixel[c] = (pixel[c] & ~1) | bits[bitIndex++];
    }
    if (bitIndex >= bits.size())
      return;
  }
}

void embedMessage() {
  string inputVideoPath, outputVideoPath, message, key;
  int frameMode, pixelMode, encryptFlag;

  cout << "=== EMBED MODE ===\n";
  cout << "Frame Mode:\n1. Sequential\n2. Random\nChoose: ";
  cin >> frameMode;
  cout << "Pixel Mode:\n1. Sequential\n2. Random\nChoose: ";
  cin >> pixelMode;
  cout << "Use Vigenere encryption? (1 = Yes / 0 = No): ";
  cin >> encryptFlag;

  if (encryptFlag) {
    cout << "Enter key: ";
    cin >> key;
  }

  cout << "Input video path: ";
  cin >> inputVideoPath;
  cout << "Output video path (should end with .avi): ";
  cin >> outputVideoPath;
  original_path = inputVideoPath;
  stego_path = outputVideoPath;

  cout << "Enter message: ";
  cin.ignore();
  getline(cin, message);

  if (encryptFlag) {
    message = vigenereEncrypt(message, key);
  }
  message += '\0'; // Null terminator to mark end
  vector<bool> messageBits = messageToBits(message);
  uint32_t messageBitLength = messageBits.size();
  vector<bool> lengthBits = getLengthBits(messageBitLength);

  VideoCapture cap(inputVideoPath);
  if (!cap.isOpened()) {
    cerr << "Failed to open input video.\n";
    return;
  }

  int frameWidth = static_cast<int>(cap.get(CAP_PROP_FRAME_WIDTH));
  int frameHeight = static_cast<int>(cap.get(CAP_PROP_FRAME_HEIGHT));
  double fps = cap.get(CAP_PROP_FPS);
  int totalFrames = static_cast<int>(cap.get(CAP_PROP_FRAME_COUNT));

  cout << "Video Info: " << frameWidth << "x" << frameHeight << ", " << fps
       << " FPS, " << totalFrames << " frames\n";

  VideoWriter writer;
  int codec = VideoWriter::fourcc('F', 'F', 'V', '1');
  writer.open(outputVideoPath, codec, fps, Size(frameWidth, frameHeight), true);

  if (!writer.isOpened()) {
    cerr << "Failed to open output video.\n";
    return;
  }
  int seed;
  cout << "Enter seed : ";
  cin >> seed;

  uint8_t metadata =
      (frameMode == 2) << 7 | (pixelMode == 2) << 6 | (encryptFlag) << 5;
  metadata |= (seed & 0x1F); // 5 bits for seed (0-31)
  cout << "frame mode : " << frameMode << " pixel mode : " << pixelMode
       << " metadata : " << static_cast<int>(metadata) << std::endl;
  int bitIndex = 0;
  mt19937 rng(seed);

  // Generate frame order if random
  vector<int> frameIndices(totalFrames);
  iota(frameIndices.begin(), frameIndices.end(), 0);
  if (frameMode == 2)
    shuffle(frameIndices.begin() + 1, frameIndices.end(),
            rng); // Skip first frame for metadata

  for (int i = 0; i < totalFrames; ++i) {
    int frameNumber = (frameMode == 2) ? frameIndices[i] : i;
    cap.set(CAP_PROP_POS_FRAMES, frameNumber);

    Mat frame;
    cap.read(frame);
    if (frame.empty())
      break;

    if (frameNumber == 0) {

      embedMetadata(frame, metadata);

      for (int p = 0; p < 32; p++) {
        int pixelIdx = p + 8;
        int x = pixelIdx % frame.cols;
        int y = pixelIdx / frame.cols;

        if (y >= frame.rows)
          break;

        frame.at<Vec3b>(y, x)[0] =
            (frame.at<Vec3b>(y, x)[0] & ~1) | lengthBits[p];
      }
      cout << "[*] Metadata + length embedded in first frame.\n";
    } else {
      embedBitsInFrame(frame, messageBits, bitIndex, (pixelMode == 2), rng);
    }

    writer.write(frame);

    if (bitIndex >= messageBits.size()) {
      cout << "[*] Message completely embedded!\n";
      // Fill remaining frames without changes
      for (int j = i + 1; j < totalFrames; ++j) {
        cap.set(CAP_PROP_POS_FRAMES, (frameMode == 2) ? frameIndices[j] : j);
        Mat restFrame;
        if (!cap.read(restFrame) || restFrame.empty())
          break;
        writer.write(restFrame);
      }
      break;
    }
  }

  if (bitIndex < messageBits.size()) {
    cerr << "[!] Not enough space to embed the entire message!\n";
  } else {
    cout << "[*] Message embedded successfully!\n";
  }

  cap.release();
  writer.release();
}

// ====================== Extract Functions ======================
uint8_t extractMetadata(const Mat &frame) {
  uint8_t metadata = 0;

  // Check if the frame is wide enough
  if (frame.cols < 8) {
    throw std::runtime_error("Frame too small to contain metadata!");
  }

  for (int p = 0; p < 8; p++) {
    // Extract LSB from the blue channel of the first row
    uint8_t lsb = frame.at<Vec3b>(0, p)[0] & 1;

    // Shift it to its position and OR it into metadata
    metadata |= (lsb << p); // little-endian
    // metadata |= (lsb << (7 - p)); // big-endian (if you prefer)
  }

  return metadata;
}

uint32_t extractLength(const Mat &frame) {
  uint32_t lengthBits = 0;
  for (int p = 0; p < 32; p++) {
    int pixelIdx = p + 8;
    int x = pixelIdx % frame.cols;
    int y = pixelIdx / frame.cols;
    if (y >= frame.rows)
      break;
    lengthBits |= (frame.at<Vec3b>(y, x)[0] & 1) << (31 - p);
  }
  return lengthBits;
}

void extractMessageBits(vector<bool> &bits, VideoCapture &cap, int totalBits,
                        bool pixelMode, bool frameMode, int totalFrames,
                        uint32_t seed) {

  cout << "Seed : " << seed << std::endl;
  mt19937 rng(seed);

  vector<int> frameIndices(totalFrames);
  iota(frameIndices.begin(), frameIndices.end(), 0);
  if (frameMode)
    shuffle(frameIndices.begin() + 1, frameIndices.end(), rng);

  // Start from frame 1, since frame 0 contains metadata
  for (int i = 1; i < totalFrames && totalBits > 0; ++i) {
    int frameNumber = (frameMode) ? frameIndices[i] : i;
    cap.set(CAP_PROP_POS_FRAMES, frameNumber);

    Mat frame;
    if (!cap.read(frame) || frame.empty())
      break;

    int rows = frame.rows;
    int cols = frame.cols;
    vector<pair<int, int>> pixelIndices;

    for (int y = 0; y < rows; ++y)
      for (int x = 0; x < cols; ++x)
        pixelIndices.push_back({y, x});

    if (pixelMode)
      shuffle(pixelIndices.begin(), pixelIndices.end(), rng);

    for (auto [y, x] : pixelIndices) {
      Vec3b pixel = frame.at<Vec3b>(y, x);
      for (int c = 0; c < 3; ++c) {
        if (totalBits <= 0)
          return;
        bits.push_back(pixel[c] & 1);
        totalBits--;
      }
      if (totalBits <= 0)
        return;
    }
  }
}

void extractRandomBits(const cv::Mat &frame, std::vector<uint8_t> &bits,
                       uint32_t messageLength, uint32_t seed) {
  cout << "Seed : " << seed << std::endl;
  std::mt19937 rng(seed); // Must match embedding RNG seed!

  int totalBits = messageLength * 8;
  for (int i = 0; i < totalBits; ++i) {
    int x = rng() % frame.cols;
    int y = rng() % frame.rows;

    uint8_t bit = frame.at<cv::Vec3b>(y, x)[0] & 1;
    bits.push_back(bit);
  }
}

void extractMessage() {
  string videoPath;
  cout << "=== EXTRACT MODE ===\n";
  cout << "Enter steganographic video path: ";
  cin >> videoPath;

  VideoCapture cap(videoPath);
  if (!cap.isOpened()) {
    cerr << "Error opening video.\n";
    return;
  }

  int totalFrames = static_cast<int>(cap.get(CAP_PROP_FRAME_COUNT));

  Mat frame;
  if (!cap.read(frame)) {
    cerr << "Failed to read first frame.\n";
    return;
  }

  uint8_t metadata = extractMetadata(frame);
  uint32_t messageLengthBits = extractLength(frame);
  cout << "Metadata : " << static_cast<int>(metadata) << std::endl;
  bool frameMode = (metadata >> 7) & 1;
  bool pixelMode = (metadata >> 6) & 1;
  bool encryptFlag = (metadata >> 5) & 1;
  uint32_t seed = metadata & 0x1F;

  cout << "[*] Metadata extracted:\n";
  cout << "Frame mode: " << (frameMode ? "Random" : "Sequential") << endl;
  cout << "Pixel mode: " << (pixelMode ? "Random" : "Sequential") << endl;
  cout << "Encryption: " << (encryptFlag ? "Yes" : "No") << endl;
  cout << "Message length: " << messageLengthBits << " bits ("
       << messageLengthBits / 8 << " bytes)\n";
  string key;
  if (encryptFlag) {
    cout << "Enter key: ";
    cin >> key;
  }

  vector<bool> messageBits;
  extractMessageBits(messageBits, cap, messageLengthBits, pixelMode, frameMode,
                     totalFrames, seed);

  string message = bitsToMessage(messageBits);
  if (encryptFlag) {
    message = vigenereDecrypt(message, key);
  }

  if (encryptFlag) {
    cout << "[!] Encryption detected\n";
  }

  int saveMsg = 0;
  cout << "Safe Message? 1.No, 2.Yes : ";
  cin >> saveMsg;

  cout << "\nExtracted Message:\n" << message << endl;
  if (saveMsg == 2) {
    string pathFile;
    cout << "Enter path file to save : ";
    cin >> pathFile;
    std::ofstream outFile(pathFile);
    // Check if the file opened successfully
    if (!outFile) {
      std::cerr << "Error opening file!" << std::endl;
    }

    // Write a line to the file
    outFile << message << std::endl;

    outFile.close();
  }

  cap.release();
}

// Fungsi untuk menghitung PSNR antara dua frame
double getPSNR(const Mat &I1, const Mat &I2) {
  if (I1.empty() || I2.empty()) {
    cerr << "Empty Frame!" << endl;
    return -1;
  }

  if (I1.size() != I2.size() || I1.type() != I2.type()) {
    cerr << "Error in frame size!" << endl;
    return -1;
  }

  Mat s1;
  absdiff(I1, I2, s1);
  s1.convertTo(s1, CV_32F);
  s1 = s1.mul(s1);

  Scalar s = sum(s1);

  double sse = s.val[0] + s.val[1] + s.val[2];

  if (sse <= 1e-10) {
    return INFINITY;
  } else {
    double mse = sse / (double)(I1.channels() * I1.total());
    double psnr = 10.0 * log10((255 * 255) / mse);
    return psnr;
  }
}

void checkPSNR() {

  VideoCapture capOriginal(original_path);
  VideoCapture capStego(stego_path);

  if (!capOriginal.isOpened() || !capStego.isOpened()) {
    cerr << "Failed to open one of the videos!" << endl;
  }

  int totalFrames = min((int)capOriginal.get(CAP_PROP_FRAME_COUNT),
                        (int)capStego.get(CAP_PROP_FRAME_COUNT));

  cout << "Total frames to analyze: " << totalFrames << endl;

  double totalPSNR = 0.0;
  int processedFrames = 0;

  for (int i = 0; i < totalFrames; ++i) {
    Mat frameOriginal, frameStego;

    capOriginal >> frameOriginal;
    capStego >> frameStego;

    if (frameOriginal.empty() || frameStego.empty()) {
      cout << "End of video at frame: " << i << endl;
      break;
    }

    double psnr = getPSNR(frameOriginal, frameStego);

    if (psnr >= 0 && psnr != INFINITY) {
      totalPSNR += psnr;
      processedFrames++;
      cout << "Frame " << i << ": PSNR = " << psnr << " dB" << endl;
    } else {
      cerr << "Error calculating PSNR at frame " << i << endl;
    }
  }

  if (processedFrames > 0) {
    double averagePSNR = totalPSNR / processedFrames;
    cout << "\n=== SUMMARY ===" << endl;
    cout << "Frames analyzed: " << processedFrames << endl;
    cout << "Average PSNR: " << averagePSNR << " dB" << endl;
  } else {
    cout << "No frames were processed!" << endl;
  }

  capOriginal.release();
  capStego.release();
}

// ====================== Main Program ======================
int main() {
  int mode;
  cout << "===== VIDEO STEGANOGRAPHY TOOL =====\n";
  cout << "Choose Mode:\n1. Embed Message\n2. Extract Message\nSelect: ";
  cin >> mode;

  if (mode == 1) {
    embedMessage();
    checkPSNR();
  } else if (mode == 2) {
    extractMessage();
  } else {
    cerr << "Invalid mode selected.\n";
  }

  return 0;
}
