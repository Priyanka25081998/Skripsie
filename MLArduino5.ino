#include <TimeLib.h>
#include <TinyGPS.h>       // http://arduiniana.org/libraries/TinyGPS/
#include <SD.h>
#include <Wire.h>
#include "Adafruit_ADT7410.h"
Adafruit_ADT7410 tempsensor = Adafruit_ADT7410();

#include "TensorFlowLite.h"
#include "tensorflow/lite/experimental/micro/kernels/micro_ops.h"
#include "tensorflow/lite/experimental/micro/micro_error_reporter.h"
#include "tensorflow/lite/experimental/micro/micro_interpreter.h"
#include "tensorflow/lite/experimental/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/version.h"

#include "model5.h"
#define DEBUG 1

// TinyGPS and SoftwareSerial libraries are the work of Mikal Hart
TinyGPS gps; 
File dataFile;
#define SerialGPS Serial1
const int offset = 2;  
float milli = 0;
float c = 0;
int t = 0;
unsigned long timestamp = 0;
float x_val = 0;
float y_val = 0;
const int chipSelect = BUILTIN_SDCARD;

// GPS PPS on pin 12  GPIO2 1  B0_01  time with GPT1
// clock(1) 24mhz   clock(4) 32khz   set  CLKSRC
// clock(5) doesn't tick
// 24mhz  PREDIV 0 TPS 24000000,  PREDIV 5  TPS 4000000
// @150 mhz PREDIV 0 TPS 150000000 to get 150 mhz

#define CLKSRC 4    // 1 or 4
#if CLKSRC == 1
#define TPS 150000000
#define PREDIV 0
#elif CLKSRC == 4
#define TPS 32768
#define PREDIV 0
#else
#error choose CLKSRC 1 or 4
#endif

uint32_t gpt_ticks() {
  return GPT1_CNT;
}

volatile uint32_t pps, ticks;
void pinisr() {
  pps = 1;
  ticks = gpt_ticks();
}

// TFLite globals, used for compatibility with Arduino-style sketches
namespace {
  tflite::ErrorReporter* error_reporter = nullptr;
  const tflite::Model* model = nullptr;
  tflite::MicroInterpreter* interpreter = nullptr;
  TfLiteTensor* model_input = nullptr;
  TfLiteTensor* model_output = nullptr;
  // Create an area of memory to use for input, output, and other TensorFlow
  // arrays. You'll need to adjust this by combiling, running, and looking
  // for errors.
  constexpr int kTensorArenaSize = 5 * 1024;
  uint8_t tensor_arena[kTensorArenaSize];
} // namespace

void setup() {
  Serial.begin(9600);
  while (!Serial) ; 
  SerialGPS.begin(9600);
  Serial.println("Waiting for GPS time ... ");
  //SD Initialization
   if (!SD.begin(chipSelect)) {
    Serial.println("SD initialization failed");
    return;
  }
  Serial.println("SD initialization success");
  tempsensor.begin();
  if (!tempsensor.begin()) {
    Serial.println("Couldn't find ADT7410!");
    while (1);
  }
  // uncomment following for 150mhz
  CCM_CSCMR1 &= ~CCM_CSCMR1_PERCLK_CLK_SEL; // turn off 24mhz mode
  CCM_CCGR1 |= CCM_CCGR1_GPT(CCM_CCGR_ON) ;  // enable GPT1 module
  GPT1_CR = 0;
  GPT1_PR = PREDIV;   // prescale+1 /1 for 32k,  /24 for 24mhz   /24 clock 1
  GPT1_SR = 0x3F; // clear all prior status
  GPT1_CR = GPT_CR_EN | GPT_CR_CLKSRC(CLKSRC) | GPT_CR_FRR ;// 1 ipg 24mhz  4 32khz
  attachInterrupt(12, pinisr, RISING);

  // Set up logging (will report to Serial, even within TFLite functions)
  static tflite::MicroErrorReporter micro_error_reporter;
  error_reporter = &micro_error_reporter;

  // Map the model into a usable data structure
  model = tflite::GetModel(model2);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    error_reporter->Report("Model version does not match Schema");
    while(1);
  }

  static tflite::MicroMutableOpResolver micro_mutable_op_resolver;
  micro_mutable_op_resolver.AddBuiltin(
    tflite::BuiltinOperator_FULLY_CONNECTED,
    tflite::ops::micro::Register_FULLY_CONNECTED(),
    1, 3);

  // Build an interpreter to run the model
  static tflite::MicroInterpreter static_interpreter(
    model, micro_mutable_op_resolver, tensor_arena, kTensorArenaSize,
    error_reporter);
  interpreter = &static_interpreter;

  // Allocate memory from the tensor_arena for the model's tensors
  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    error_reporter->Report("AllocateTensors() failed");
    while(1);
  }

  // Assign model input and output buffers (tensors) to pointers
  model_input = interpreter->input(0);
  model_output = interpreter->output(0);

#if DEBUG
  Serial.print("Number of dimensions: ");
  Serial.println(model_input->dims->size);
  Serial.print("Dim 1 size: ");
  Serial.println(model_input->dims->data[0]);
  Serial.print("Dim 2 size: ");
  Serial.println(model_input->dims->data[1]);
  Serial.print("Input type: ");
  Serial.println(model_input->type);
#endif
}

void loop() {
  dataFile = SD.open("GPT4.txt", FILE_WRITE);
  syncWithGPS();
  dataFile.close();
}

void syncWithGPS(){
  while (SerialGPS.available()) {
    if (gps.encode(SerialGPS.read())) { // process gps messages
      // when TinyGPS reports new data...
      unsigned long age;
      int year;
      byte month, day, hour, minute, second;
      static uint32_t prev = 0;

      if (pps) {
        // set the Time to the latest GPS reading
        gps.crack_datetime(&year, &month, &day, &hour, &minute, &second, NULL, &age);
        setTime(hour, minute, second, day, month, year);
        adjustTime(offset * SECS_PER_HOUR);
        if (prev != 0) {
          t = ticks - prev;
          milli = (ticks/TPS)*1000;
          timestamp = milli;
          c = tempsensor.readTempC();
        
          x_val = (float)timestamp;
          model_input->data.f[0] = x_val;
          TfLiteStatus invoke_status = interpreter->Invoke();
          if (invoke_status != kTfLiteOk) {
            error_reporter->Report("Invoke failed on input: %f\n", x_val);
          }
          y_val = (float)timestamp + model_output->data.f[0];
        
          GPSwriteSD();
        }
        pps = 0;
        prev = ticks;
        c = 0;
      }
    }
  }
}

void GPSwriteSD(){
   dataFile.print("GPS");
   dataFile.print(",");
   dataFile.print(hour());
   printDigitsSD(minute());
   printDigitsSD(second());
   dataFile.print(",");
   dataFile.print(day());
   dataFile.print(",");
   dataFile.print(month());
   dataFile.print(",");
   dataFile.print(year());
   dataFile.print(",");
   dataFile.print(timestamp);
   dataFile.print(",");
   dataFile.print(c);
   dataFile.print(",");  
   dataFile.print(y_val);
   dataFile.print(",");
   dataFile.println(t);
}

void printDigitsSD(int digits) {
  if(digits < 10)
    dataFile.print('0');
  dataFile.print(digits);
}
