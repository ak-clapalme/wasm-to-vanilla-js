#include <emscripten.h>
#include <emscripten/bind.h>
#include <cstdint>
#include <random>
#include <iostream>
#include <string>
#include <emscripten/val.h>

// if multithreading is enabled
#ifdef __EMSCRIPTEN_PTHREADS__
#include <chrono>
#include <mutex>
#include <queue>
#include <thread>

std::thread t1;

#endif

EM_JS(void, drawCanvas, (int height, int width), {
		body = document.getElementsByTagName("body")[0];
		canvas = document.createElement("canvas");
		body.appendChild(canvas);
		canvas.setAttribute("id", "canvas");
		canvas.setAttribute("height", height);
		canvas.setAttribute("width", width);
});

class DebugLog {
	public:
		#ifdef __EMSCRIPTEN_PTHREADS__
		DebugLog() {
			t1 = std::thread(&DebugLog::waitForMessagesToLog, this);
		}

		void waitForMessagesToLog() {
			while (true) {
				std::unique_lock lck(messages_mutex);
				if (!messages.empty()) {
					auto[str, name] = messages.front();
					messages.pop();
					lck.unlock();
					_log(str, name);
				}
				using namespace std::literals::chrono_literals;
				std::this_thread::sleep_for(500ms);
			}
		}
		#endif

		void logMessage(const std::string& str, const std::string& name) {
			#ifdef __EMSCRIPTEN_PTHREADS__
				std::lock_guard g(messages_mutex);
				messages.push({str, name});
			#else
				_log(str, name);
				#endif
		}

		void _log(const std::string& str, const std::string& name) {
			std::cout << "hello, this is the single-threaded logging mechanism" << std::endl;
		}

	private:
		#ifdef __EMSCRIPTEN_PTHREADS__
		std::queue<std::pair<std::string, std::string>> messages;
		std::mutex messages_mutex;
		#endif
};

enum class Move {
	STATIONARY = 0,
	UP = 1,
	DOWN = 2
};

struct Paddle {
	float xpos;
	float ypos;

	void moveUp() {
		if (ypos > 50) {
			ypos--;
		}
	}

	void moveDown() {
		if (ypos < 550) {
			ypos++;
		}
	}

	bool isAtPaddleLevel(float ypos) {
		return (ypos - 5 < this->ypos + 50) && (ypos + 5 > this->ypos - 50);
	}
};

struct Ball {
	float xpos = 395;
	float ypos = 295;
	float xspeed = 1;
	float yspeed = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);

	bool isAtTopOrBottom() {
		return ypos - 5 < 0 || ypos + 5 > 595;
	}

	bool scoresOnRight() {
		return xpos > 775;
	}

	bool scoresOnLeft() {
		return xpos < 25;
	}

	bool doesHitPaddle(Paddle left, Paddle right) {
		return (xpos - 5 < 50 && left.isAtPaddleLevel(ypos)) || (xpos + 5 > 750 && right.isAtPaddleLevel(ypos));
	}

	void update() {
		xpos += xspeed;
		ypos += yspeed;
	}
};

struct GameState {
	std::string name;
	Ball ball{};
	Paddle left{25, 300};
	Paddle right{750, 300};
	Move move = Move::STATIONARY;
	int leftScore = 0;
	int rightScore = 0;
};

float calculateReflectionFactor(Ball ball, Paddle left, Paddle right) {
	Paddle paddle = (ball.xpos > 400) ? right : left;
	const float factor = ball.ypos - paddle.ypos;
	return factor / 100;
}

void makeAIMove(Ball ball, Paddle& leftPaddle) {
	
	int idealPosition = ball.ypos;
	
	if (ball.xspeed <= 0) {
		auto turns = (ball.xpos - 50) / (-1 * ball.xspeed);
		idealPosition = ball.ypos + (ball.yspeed * turns);
	}

	if (idealPosition > leftPaddle.ypos) {
		leftPaddle.moveDown();
	}
	if (idealPosition < leftPaddle.ypos) {
		leftPaddle.moveUp();
	}
}

GameState createInitialGameState(const std::string& name) {
	drawCanvas(600, 800);
	return GameState{name};
}

GameState updatePosition(GameState gameState) {
	if (gameState.ball.xspeed == 0 && gameState.ball.yspeed == 0) {
		gameState.ball.xspeed = 1;
	}
	if (gameState.move == Move::UP) {
		gameState.right.moveUp();
	}
	if (gameState.move == Move::DOWN) {
		gameState.right.moveDown();
	}
	
	makeAIMove(gameState.ball, gameState.left);

	if (gameState.ball.isAtTopOrBottom()) {
		gameState.ball.yspeed = -gameState.ball.yspeed;
	}
	if (gameState.ball.doesHitPaddle(gameState.left, gameState.right)) {
		gameState.ball.xspeed = -gameState.ball.xspeed;
		gameState.ball.xspeed *= 1.05;
		gameState.ball.yspeed += calculateReflectionFactor(gameState.ball, gameState.left, gameState.right);
	}

	if (gameState.ball.scoresOnRight()) {
		gameState.ball = Ball{};
		gameState.leftScore += 1;
	}
	if (gameState.ball.scoresOnLeft()) {
		gameState.ball = Ball{};
		gameState.rightScore += 1;
	}

	gameState.ball.update();

	return gameState;
}	

// advertises which functions will be available to the javascript
// aka: I want an emscript function called getAIMove and I give it the address of the cpp getAIMove
EMSCRIPTEN_BINDINGS(gameState) {
	emscripten::value_object<GameState>("GameState")
		.field("ball", &GameState::ball)
		.field("left", &GameState::left)
		.field("right", &GameState::right)
		.field("move", &GameState::move)
		.field("leftScore", &GameState::leftScore)
		.field("rightScore", &GameState::rightScore)
		.field("name", &GameState::name);
	
	emscripten::enum_<Move>("Move")
		.value("STATIONARY", Move::STATIONARY)
		.value("UP", Move::UP)
		.value("DOWN", Move::DOWN);
	
	emscripten::value_object<Paddle>("Paddle")
		.field("ypos", &Paddle::ypos)
		.field("xpos", &Paddle::xpos);

	emscripten::class_<Ball>("Ball")
		.constructor<>()
		.property("ypos", &Ball::ypos)
		.property("xpos", &Ball::xpos)
		.property("yspeed", &Ball::yspeed)
		.property("xspeed", &Ball::xspeed);

	emscripten::function("updatePosition", &updatePosition);
	emscripten::function("createInitialGameState", &createInitialGameState);
}

