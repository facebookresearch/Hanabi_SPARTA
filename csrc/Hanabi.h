// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#ifndef H_HANABI_SERVER
#define H_HANABI_SERVER

#include <cassert>
#include <string>
#include <ostream>
#include <random>
#include <vector>
#include <tuple>
#include "ThreadPool.h"
#include <future>


namespace Params {
  std::string getParameterString(
    const std::string &name,
    std::string default_val,
    const std::string help=""
  );
  int getParameterInt(
    const std::string &name,
    int default_val,
    const std::string help=""
  );
  float getParameterFloat(
    const std::string &name,
    float default_val,
    const std::string help=""
  );
}

namespace HanabiParams {
  // read in parameters from environment variables

  // return a score of 0 when bombing out
  const int BOMB0 = Params::getParameterInt("BOMB0", 0,
    "If 1, then the score is 0 if agents bomb out (official rules).");
  const int BOMBD = Params::getParameterInt("BOMBD", 1,
    "Subtract this number of points from the score when bombing out.");
  const int FIBER_THREADS = Params::getParameterInt("FIBER_THREADS", 10,
    "Number of threads in the thread pool that executes fibers (i.e. # cores to use).");
  const int NUM_THREADS = Params::getParameterInt("NUM_THREADS", 1000,
    "Number of user-space threads (i.e. fibers) to use for search (i.e. max parallelism). "
    "These fibers are run on the fiber thread pool defined by FIBER_THREADS");
  const int HAND_SIZE_OVERRIDE = Params::getParameterInt("HAND_SIZE_OVERRIDE", -1,
    "If >0, this overrides the hand size. Must be >= 3.");
} // namespace HanabiParams


namespace Hanabi {
    class Card;
    class Pile;
    class Server;
    class Bot;
    class BotFactory;
}  /* namespace Hanabi */

namespace Hanabi {

inline ThreadPool &getThreadPool() {
  static std::shared_ptr<ThreadPool> pool;
  if (!pool || pool->stop) {
    pool.reset(new ThreadPool(HanabiParams::FIBER_THREADS));
  }
  return *pool;
}

class ServerError : public std::runtime_error {
public:
  explicit ServerError( const std::string& what_arg ) : runtime_error(what_arg) {}
};

/* Bots may assume that these correspond to the indices 0..4. */
typedef enum: int8_t { RED=0, ORANGE, YELLOW, GREEN, BLUE, INVALID_COLOR } Color;
constexpr int NUMCOLORS = 5;

/* Bots may assume that these correspond to their integer values. */
typedef enum : int8_t { ONE=1, TWO, THREE, FOUR, FIVE } Value;
constexpr int VALUE_MAX=5;

constexpr int NUMHINTS = 8;
constexpr int NUMMULLIGANS = 3;

inline Color &operator++ (Color &c) { c = Color((int)c+1); return c; }
inline Color operator++ (Color &c, int) { Color oc = c; ++c; return oc; }
inline Value &operator++ (Value &v) { v = Value((int)v+1); return v; }
inline Value operator++ (Value &v, int) { Value ov = v; ++v; return ov; }
inline Value &operator-- (Value &v) { v = Value((int)v-1); return v; }
inline Value operator-- (Value &v, int) { Value ov = v; --v; return ov; }

class Card {
public:
    Color color;
    Value value;
  #ifdef CARD_ID
    int16_t id; // just for keeping track of cards in hand for UI
  #endif
    Card(Color c, Value v);
    Card(Color c, int v);
    int count() const;
    std::string toString() const;

    bool operator== (const Card &rhs) const
    { return (this->color == rhs.color) && (this->value == rhs.value); }

    bool operator!= (const Card &rhs) const
    { return !(*this == rhs); }

    bool operator< (const Card &rhs) const
    {
      return (this->color < rhs.color) || (this->color == rhs.color && this->value < rhs.value);
      // static_assert(sizeof(Card) == sizeof(int16_t));
      // return (*(reinterpret_cast<const int16_t*>(this))) < (*(reinterpret_cast<const int16_t*>(&rhs)));
    } //&return std::tie(color, value) < std::tie(rhs.color, rhs.value); }
};

class Pile {
private:
    Color color;
    int size_;  /* might be zero */
    void increment_();
    friend class Server;
public:
    bool empty() const { return size_ == 0; }
    int size() const { return size_; }
    Card topCard() const;  /* throws if the pile is empty */
    bool nextValueIs(int value) const { return value == size_+1; }
    bool contains(int value) const { return (1 <= value && value <= size_); }
};

class CardIndices {
    unsigned int mask_;
    int count_;
    friend class Server;
public:
    explicit CardIndices() : mask_(0), count_(0) {}
    void add(int index) { mask_ |= (1 << index); count_ += 1; }
    bool contains(int index) const {
        if (0 <= index && index < 8) {
            return (mask_ & (1u << index)) != 0;
        } else {
            return false;
        }
    }
    int operator[](int i) const {
        assert(0 <= i && i < count_);
        int index = 0;
        while (true) {
            while (!contains(index)) ++index;
            if (i-- == 0) return index;
            ++index;
        }
    }
    int size() const { return count_; }
    bool empty() const { return (mask_ == 0); }
};

class Server {
public:
    Server();
    virtual ~Server() { }

    /* Set up logging, for debuggability and amusement. */
    void setLog(std::ostream *logStream);

    /* Seed the random number generator. */
    void srand(unsigned int seed);

    /* Set up a new game, using numPlayers player-bots as created
     * by repeated calls to botFactory.create(i,numPlayers). Then
     * run the game to its conclusion, and return the final score. */
    int runGame(const BotFactory &botFactory, int numPlayers);

    /* If a pre-shuffled deck is provided, we'll use a copy of
     * that deck instead of randomly shuffling our own. The "top card"
     * of the input deck should be at vector index 0; i.e., the cards
     * should be listed in the order that they will be drawn during
     * the game. */
    int runGame(const BotFactory &botFactory, int numPlayers, const std::vector<Card>& stackedDeck);

    /* Runs a game with an existing vector of bots. Caller is responsible
       for creating and deleting them.
    */
    int runGame(std::vector<Bot*>, const std::vector<Card>& stackedDeck);

    /* Runs an already set-up game to completion, and returns the score */
    int runToCompletion();

    void endGameByBombingOut();

    /* Returns the number of players in the game. */
    int numPlayers() const;

    /* Returns the starting hand size for this game. */
    int handSize() const;

    /* Returns the index of the player who is currently
     * querying the server. */
    int whoAmI() const;

    /* Returns the index of the player whose turn it is.
     * Players are numbered around to the left. The first
     * player to take a turn is player number 0. */
    int activePlayer() const;

    /* Returns the number of cards in some player's hand. */
    int sizeOfHandOfPlayer(int player) const;

    /* Returns a vector of the cards in some player's hand.
     * Throws an exception if a player tries to look at his own hand,
     * i.e., server.handOfPlayer(server.whoAmI()). */
    const std::vector<Card>& handOfPlayer(int player) const;

    /* Convenience method to keep track of card identity across turns */
    const std::vector<int> cardIdsOfHandOfPlayer(int player) const;

    /* Returns the card about to be played or discarded. This allows
     * the active player to observe his own play from within
     * Bot::pleaseObservePlay() or Bot::pleaseObserveDiscard().
     * Throws an exception if called from anywhere that is not either
     * of those functions. */
    Card activeCard() const;

    /* Observe the pile of the given color. */
    Pile pileOf(Color color) const;

    /* Returns a vector of the discards over the whole game.
     * The first card ever discarded is v[0]; the latest
     * card discarded is v[v.size()-1].
     * Discards include any cards that were misplayed as a
     * result of a bad pleasePlay(), in addition to any cards
     * that were explicitly discarded with pleaseDiscard(). */
    const std::vector<Card>& discards() const;

    /* Returns the number of hint-stones used (by all players),
     * or the number remaining in the middle of the table.
     * These two numbers always sum to NUMHINTS. */
    int hintStonesUsed() const;
    int hintStonesRemaining() const;

    /* Returns true if discarding is allowed; that is, if
     * there is at least one hint-stone used at the moment. */
    bool discardingIsAllowed() const;

    /* Returns the number of mulligan-stones used,
     * or the number remaining in the middle of the table.
     * These two numbers always sum to NUMMULLIGANS. */
    int mulligansUsed() const;
    int mulligansRemaining() const;

    /* Returns the number of cards remaining to be drawn. */
    int cardsRemainingInDeck() const;

    int finalCountdown() const;

    /* Returns TRUE if no more calls to pleaseMakeMove()
     * can be made during this game. */
    bool gameOver() const;

    /* Return the current score, which is just the sum of the
     * values of the top cards in every pile. */
    int currentScore() const;


    /*================= MUTATORS =============================*/

    /* Try to discard the card at the given index in
     * the active player's hand. This action invariably
     * succeeds. The other cards will be shifted down
     * (toward index 0) and a new card drawn into index 3,
     * unless no cards remain in the draw pile, in which
     * case the player's new hand will have only 3 cards.
     * Adds a hint-stone back to the pool, if possible.
     */
    virtual void pleaseDiscard(int index);

    /* Try to play the card at the given index. This action
     * may succeed (incrementing the appropriate pile) or fail
     * (adding the selected card to the end of server.discards()).
     * In either case, the other cards will be shifted down
     * and a new card drawn into index 3, unless no cards
     * remain in the draw pile, in which case the player's
     * new hand will have only 3 cards.
     */
    virtual void pleasePlay(int index);

    /* Give a hint. The named player (who must not be the
     * active player) must have at least one card of the
     * specified color in his hand.
     * Throws an exception if there are no hint-stones left. */
    virtual void pleaseGiveColorHint(int player, Color color);

    /* Give a hint. The named player (who must not be the
     * active player) must have at least one card of the
     * specified value in his hand.
     * Throws an exception if there are no hint-stones left. */
    virtual void pleaseGiveValueHint(int player, Value value);

    /*================= DEBUGGING TOOLS ======================*/

    std::string handsAsString() const;
    std::string pilesAsString() const;
    std::string discardsAsString() const;
    /* This exposes any player's hand, including the observing player.
     * This should only be used for debugging purposes! */
    std::vector<Card> cheatGetHand(int index) const;

    std::ostream* log() {
      return log_;
    }

    /*================= PRIVATE MEMBERS ======================*/
    int seed_;
protected:
    /* Administrivia */
    std::ostream *log_;
    std::mt19937 rand_;
    std::vector<Bot *> players_;
    int observingPlayer_;
    int activePlayer_;
    int movesFromActivePlayer_;
    Card activeCard_;
    bool activeCardIsObservable_;
    int finalCountdown_;
    /* Basically-public state */
    int numPlayers_;
    Pile piles_[NUMCOLORS];
    std::vector<Card> discards_;
    int hintStonesRemaining_;
    int mulligansRemaining_;
    /* Basically-hidden state */
    std::vector<std::vector<Card> > hands_;
    std::vector<Card> deck_;

    /* Private methods */
    Card draw_(void);
    void regainHintStoneIfPossible_(void);
    void loseMulligan_(void);
    void logHands_(void) const;
    void logPiles_(void) const;
};

}  /* namespace Hanabi */


/* These are the pieces that you must implement on your own. */

namespace Hanabi {

class Bot {
public:
    virtual ~Bot();  /* virtual destructor */
    virtual void pleaseObserveBeforeMove(const Server &) = 0;
    virtual void pleaseMakeMove(Server &) = 0;
      virtual void pleaseObserveBeforeDiscard(const Server &, int from, int card_index) = 0;
      virtual void pleaseObserveBeforePlay(const Server &, int from, int card_index) = 0;
      virtual void pleaseObserveColorHint(const Server &, int from, int to, Color color, CardIndices card_indices) = 0;
      virtual void pleaseObserveValueHint(const Server &, int from, int to, Value value, CardIndices card_indices) = 0;
    virtual void pleaseObserveAfterMove(const Server &) = 0;

    /* Return a copy of this bot with identical state.
     * The clone object is unmanaged, and must be deleted by the caller. */
    virtual Bot *clone() const { throw std::runtime_error("Not implemented."); }

    /* By default, Bots assume that they are playing with another copy of the
     * same Bot class, and may throw exceptions if that assumption is violated.
     * if permissive=true, then the Bot should degrade gracefully when 'confused'
     * by a partner that violates its assumptions. */
    virtual void setPermissive(bool permissive) { permissive_ = permissive; }

    /* hacks for playing TorchBot with humans */
    virtual const std::map<int, float> &getActionProbs() const {
      throw std::runtime_error("Not implemented.");
    }
    virtual void setActionUncertainty(float action_unc) {
      throw std::runtime_error("Not implemented.");
    }

protected:
    bool permissive_ = false;
};

class BotFactory {
public:
    virtual Bot *create(int index, int numPlayers, int handSize) const = 0;
    virtual void destroy(Bot *bot) const = 0;
    virtual ~BotFactory() = default;
};

// simple registration of BotFactory's by string key
void registerBotFactory(std::string name, std::shared_ptr<Hanabi::BotFactory> factory);
std::shared_ptr<Hanabi::BotFactory> getBotFactory(const std::string &botName);

}  /* namespace Hanabi */


#endif /* H_HANABI_SERVER */
