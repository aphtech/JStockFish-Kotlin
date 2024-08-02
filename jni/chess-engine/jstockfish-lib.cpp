// Basically, this file looks like uci.cpp file of Stockfish.

#include <iostream>
#include <sstream>
#include <string>
#include <jni.h>

#include "jstockfish/evaluate.h"
#include "jstockfish/evaluate_trace.h"
#include "jstockfish/movegen.h"
#include "jstockfish/position.h"
#include "jstockfish/positionstate.h"
#include "jstockfish/search.h"
#include "jstockfish/thread.h"
#include "jstockfish/timeman.h"
#include "jstockfish/tt.h"
#include "jstockfish/uci.h"
#include "jstockfish/syzygy/tbprobe.h"

//#include "jstockfish_Uci.h"
//#include "jstockfish_Position.h"

using namespace std;

//------------------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif

jint JNI_OnLoad(JavaVM* vm, void* reserved);

#ifdef __cplusplus
}
#endif

//------------------------------------------------------------------------------

static JavaVM* jvm = NULL;
static jclass uciClass = NULL;
static jclass globalUciClass = NULL;
static jmethodID onOutput = NULL;

// Call jstockfish.Uci.onOutput
void uci_out(string output) {
  // Should another thread need to access the Java VM, it must first call
  // AttachCurrentThread() to attach itself to the VM and obtain a JNI interface pointer
  JNIEnv *env = nullptr;
  if (jvm->AttachCurrentThread(&env, NULL) != 0) {
    cout << "[JNI] Could not AttachCurrentThread" << endl;
    return;
  }


  jstring js = env->NewStringUTF(output.c_str());
  env->CallStaticVoidMethod(globalUciClass, onOutput, js);
  jvm->DetachCurrentThread();
}

//------------------------------------------------------------------------------

// FEN string of the initial position, normal chess
static const char* START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// A list to keep track of the position states along the setup moves (from the
// start position to the position just before the search starts). Needed by
// 'draw by repetition' detection.
static StateListPtr gStates(new std::deque<StateInfo>(1));

// The root position
static Position gPos;

//------------------------------------------------------------------------------

bool initJvm(JavaVM *vm) {
  jvm = vm;

  JNIEnv *env;
  int stat = jvm->GetEnv((void **) &env, JNI_VERSION_1_6);
	if (stat != JNI_OK) {
    cout << "[JNI] Could not GetEnv" << endl;
    return false;
  }

  uciClass = env->FindClass("your_path_to/jstockfish/UCI");
	if (!uciClass) {
    cout << "[JNI] Could not find class jstockfish.Uci" << endl;
    return false;
  }
    else{
      globalUciClass = (jclass)env->NewGlobalRef(uciClass);
    }

  onOutput = env->GetStaticMethodID(uciClass, "onOutput", "(Ljava/lang/String;)V");
	if (!onOutput) {
    cout << "[JNI] Could not get method jstockfish.Uci.onOutput" << endl;
    return false;
  }

  return true;
}

bool read_position(
  JNIEnv *env, jboolean chess960, jstring position,
  StateListPtr& states, Position& pos
) {
  const char *chars = env->GetStringUTFChars(position, NULL);
  istringstream is(chars);
  env->ReleaseStringUTFChars(position, chars);

  Move m;
  string token, fen;

  is >> token;

  if (token == "startpos") {
    fen = START_FEN;
    is >> token;  // Consume "moves" token if any
  } else if (token == "fen") {
    while (is >> token && token != "moves") {
      fen += token + " ";
    }
  } else {
    return false;
  }
  pos.set(fen, chess960, &states->back(), Threads.main());

  // Parse move list (if any)
  while (is >> token) {
    m = UCI::to_move(pos, token);
    if (m == MOVE_NONE) {
      return false;
    }

    states->push_back(StateInfo());
    pos.do_move(m, states->back());
  }

  return true;
}

// https://hxim.github.io/Stockfish-Evaluation-Guide/
double whiteScore(int term) {
  double mg = Trace::scores[term][WHITE][MG];
  double eg = Trace::scores[term][WHITE][EG];

  // Ignore MG if it's too small, same for EG.
  // Otherwise return the average of the two.
  if (-0.01 < mg && mg < 0.01) return eg;
  if (-0.01 < eg && eg < 0.01) return mg;
  return (mg + eg) / 2;
}

jdoubleArray whiteScores(JNIEnv *env, Position& pos) {
  Eval::set_scores(pos);

  double arr[12];

  arr[0] = Trace::scores[Trace::TOTAL][WHITE][ALL_PIECES];

  arr[1] = whiteScore(PAWN);
  arr[2] = whiteScore(KNIGHT);
  arr[3] = whiteScore(BISHOP);
  arr[4] = whiteScore(ROOK);
  arr[5] = whiteScore(QUEEN);
  arr[6] = whiteScore(KING);

  arr[7] = whiteScore(Trace::IMBALANCE);
  arr[8] = whiteScore(Trace::MOBILITY);
  arr[9] = whiteScore(Trace::THREAT);
  arr[10] = whiteScore(Trace::PASSED);
  arr[11] = whiteScore(Trace::SPACE);

  int length = sizeof(arr) / sizeof(double);
  jdoubleArray ret = env->NewDoubleArray(length);
  env->SetDoubleArrayRegion(ret, 0, length, arr);

  return ret;
}

bool isLegal(Position& pos, JNIEnv *env, jstring move) {
  const char *chars = env->GetStringUTFChars(move, NULL);
  string str = chars;
  env->ReleaseStringUTFChars(move, chars);

  Move m = UCI::to_move(pos, str);
  return pos.pseudo_legal(m);
}

//------------------------------------------------------------------------------

namespace PSQT {
  void init();
}

jint JNI_OnLoad(JavaVM *vm, void*) {
	if (!initJvm(vm)) return JNI_VERSION_1_6;

  // See Stockfish's main function
  UCI::init(Options);
  PSQT::init();
  Bitboards::init();
  Position::init();
  Bitbases::init();
  Search::init();
  Pawns::init();
  Tablebases::init(Options["SyzygyPath"]);
  TT.resize(Options["Hash"]);
  Threads.set(Options["Threads"]);
  Search::clear();

  gPos.set(START_FEN, Options["UCI_Chess960"], &gStates->back(), Threads.main());

  return JNI_VERSION_1_6;
}

extern "C"
JNIEXPORT jstring JNICALL
Java_[package_name]_UCI_00024Companion_uci(JNIEnv *env, jobject thiz) {
  stringstream uci;
  uci << "id name " << engine_info(true)
      << "\n"       << Options
      << "\nuciok";

  jstring ret = env->NewStringUTF(uci.str().c_str());
  return ret;
}


extern "C"
JNIEXPORT jboolean JNICALL
Java_[package_name]_UCI_00024Companion_setOption(JNIEnv *env, jobject thiz,
                                                                     jstring name, jstring value) {
  const char *chars_name = env->GetStringUTFChars(name, NULL);
  string str_name = chars_name;
  env->ReleaseStringUTFChars(name, chars_name);

  const char *chars_value = env->GetStringUTFChars(value, NULL);
  string str_value = chars_value;
  env->ReleaseStringUTFChars(value, chars_value);

  if (Options.count(str_name)) {
      Options[str_name] = str_value;
      return true;
  } else {
      return false;
  }
}

extern "C"
JNIEXPORT void JNICALL
Java_[package_name]_UCI_00024Companion_newGame(JNIEnv *env, jobject thiz) {
  Search::clear();
  Tablebases::init(Options["SyzygyPath"]);
  Time.availableNodes = 0;
}


extern "C"
JNIEXPORT jboolean JNICALL
Java_[package_name]_UCI_00024Companion_position(JNIEnv *env, jobject thiz,
                                                                    jstring position) {
  gStates = StateListPtr(new std::deque<StateInfo>(1));
  return read_position(
    env, Options["UCI_Chess960"], position,
    gStates, gPos
  );
}

//------------------------------------------------------------------------------

extern "C"
JNIEXPORT void JNICALL
Java_[package_name]_UCI_00024Companion_go(JNIEnv *env, jobject thiz,
                                                              jstring options) {
  // Similar to Stockfish's UCI#go

  const char *chars = env->GetStringUTFChars(options, NULL);
  istringstream is(chars);
  env->ReleaseStringUTFChars(options, chars);

  Search::LimitsType limits;
  string token;
  bool ponderMode = false;

  limits.startTime = now();

  while (is >> token)
    if (token == "searchmoves")
      while (is >> token)
        limits.searchmoves.push_back(UCI::to_move(gPos, token));

    else if (token == "wtime")     is >> limits.time[WHITE];
    else if (token == "btime")     is >> limits.time[BLACK];
    else if (token == "winc")      is >> limits.inc[WHITE];
    else if (token == "binc")      is >> limits.inc[BLACK];
    else if (token == "movestogo") is >> limits.movestogo;
    else if (token == "depth")     is >> limits.depth;
    else if (token == "nodes")     is >> limits.nodes;
    else if (token == "movetime")  is >> limits.movetime;
    else if (token == "mate")      is >> limits.mate;
    else if (token == "infinite")  limits.infinite = 1;
    else if (token == "ponder")    ponderMode = true;

  Threads.start_thinking(gPos, gStates, limits, ponderMode);
}

extern "C"
JNIEXPORT void JNICALL
Java_[package_name]_UCI_00024Companion_stop(JNIEnv *env, jobject thiz) {
  Threads.stop = true;
}

extern "C"
JNIEXPORT void JNICALL
Java_[package_name]_UCI_00024Companion_ponderHit(JNIEnv *env, jobject thiz) {
  Threads.ponder = false;
}

//------------------------------------------------------------------------------

extern "C"
JNIEXPORT jstring JNICALL
Java_[package_name]_UCI_00024Companion_d(JNIEnv *env, jobject thiz) {
  stringstream ss;
  ss << gPos;
  jstring ret = env->NewStringUTF(ss.str().c_str());
  return ret;
}

extern "C"
JNIEXPORT void JNICALL
Java_[package_name]_UCI_00024Companion_flip(JNIEnv *env, jobject thiz) {
  gPos.flip();
}

extern "C"
JNIEXPORT jstring JNICALL
Java_[package_name]_UCI_00024Companion_eval(JNIEnv *env, jobject thiz) {
    stringstream ss;
    ss << Eval::trace(gPos);
    jstring ret = env->NewStringUTF(ss.str().c_str());
    return ret;
}

extern "C"
JNIEXPORT jdoubleArray JNICALL
Java_[package_name]_UCI_00024Companion_whiteScores(JNIEnv *env, jobject thiz) {
  return whiteScores(env, gPos);
}

//------------------------------------------------------------------------------

extern "C"
JNIEXPORT jboolean JNICALL
Java_[package_name]_UCI_00024Companion_isLegal(JNIEnv *env, jobject thiz,
                                                                   jstring move) {
  return isLegal(gPos, env, move);
}

extern "C"
JNIEXPORT jstring JNICALL
Java_[package_name]_UCI_00024Companion_fen(JNIEnv *env, jobject thiz) {
  string fen = gPos.fen();
  jstring ret = env->NewStringUTF(fen.c_str());
  return ret;
}

extern "C"
JNIEXPORT jint JNICALL
Java_[package_name]_UCI_00024Companion_ordinalState(JNIEnv *env, jobject thiz) {
  return positionstate(gPos);
}

//------------------------------------------------------------------------------

extern "C"
JNIEXPORT jdoubleArray JNICALL
Java_[package_name]_Position_00024Companion_whiteScores(JNIEnv *env,
                                                                            jobject thiz,
                                                                            jboolean chess960,
                                                                            jstring position) {
  StateListPtr states(new std::deque<StateInfo>(1));
  Position pos;
  read_position(env, chess960, position, states, pos);

  // When read_position above returns false, the result is somewhere in the middle
  return whiteScores(env, pos);
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_[package_name]_Position_00024Companion_isLegal(JNIEnv *env, jobject thiz,
                                                                        jboolean chess960,
                                                                        jstring position,
                                                                        jstring move) {
  StateListPtr states(new std::deque<StateInfo>(1));
  Position pos;
  return
    read_position(env, chess960, position, states, pos) &&
    isLegal(pos, env, move);
}

extern "C"
JNIEXPORT jstring JNICALL
Java_[package_name]_Position_00024Companion_fen(JNIEnv *env, jobject thiz,
                                                                    jboolean chess960,
                                                                    jstring position) {
  StateListPtr states(new std::deque<StateInfo>(1));
  Position pos;
  read_position(env, chess960, position, states, pos);

  // When read_position above returns false, the result is somewhere in the middle
  string fen = pos.fen();
  jstring ret = env->NewStringUTF(fen.c_str());
  return ret;
}

extern "C"
JNIEXPORT jint JNICALL
Java_[package_name]_Position_00024Companion_ordinalState(JNIEnv *env,
                                                                             jobject thiz,
                                                                             jboolean chess960,
                                                                             jstring position) {
  StateListPtr states(new std::deque<StateInfo>(1));
  Position pos;
  read_position(env, chess960, position, states, pos);

  // When read_position above returns false, the result is somewhere in the middle
  return positionstate(pos);
}
