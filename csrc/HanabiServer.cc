// Copyright (c) Facebook, Inc. and its affiliates.
// All rights reserved.
//
// This source code is licensed under the license found in the
// LICENSE file in the root directory of this source tree.

#include <algorithm>
#include <cassert>
#include <ostream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>
#include "Hanabi.h"

#ifdef HANABI_SERVER_NDEBUG
#define HANABI_SERVER_ASSERT(x, msg) (void)0
#else
#define HANABI_SERVER_ASSERT(x, msg) do { if (!(x)) throw ServerError(msg); } while (0)
#endif

namespace Params {

std::string getParameterString(const std::string &name, std::string default_val, const std::string help) {
  static std::map<std::string, std::string> memoized;
  if (memoized.count(name)) {
    return memoized.at(name);
  }
  char *val = getenv(name.c_str());
  std::string ret = (val && std::string(val) != "") ? std::string(val) : default_val;
  std::cerr << name << ": " << ret << std::endl;
  if (help != "") {
    std::cerr << "\t" << help << std::endl;
  }
  memoized[name] = ret;
  return ret;
}

int getParameterInt(const std::string &name, int default_val, const std::string help) {
  std::string val = getParameterString(name, std::to_string(default_val), help);
  return stoi(val);
}

float getParameterFloat(const std::string &name, float default_val, const std::string help) {
  std::string val = getParameterString(name, std::to_string(default_val), help);
  return std::stof(val);
}

}


using namespace HanabiParams;

static std::string nth(int n, int total)
{
    if (total == 5) {
        switch (n) {
            case 0: return "oldest";
            case 1: return "second-oldest";
            case 2: return "middle";
            case 3: return "second-newest";
            default: assert(n == 4); return "newest";
        }
    } else if (total == 4) {
        switch (n) {
            case 0: return "oldest";
            case 1: return "second-oldest";
            case 2: return "second-newest";
            default: assert(n == 3); return "newest";
        }
    } else {
        switch (n) {
            case 0: return "oldest";
            case 1: return "middle";
            default: assert(n == 2); return "newest";
        }
    }
}

static std::string nth(const Hanabi::CardIndices& ns, int total)
{
    std::string result;
    assert(!ns.empty());
    switch (ns.size()) {
        case 1: return nth(ns[0], total);
        case 2: return nth(ns[0], total) + " and " + nth(ns[1], total);
        case 3: return nth(ns[0], total) + ", " + nth(ns[1], total) + ", and " + nth(ns[2], total);
        default:
            assert(ns.size() == 4);
            return nth(ns[0], total) + ", " + nth(ns[1], total) + ", " + nth(ns[2], total) + ", and " + nth(ns[3], total);
    }
}

static std::string colorname(Hanabi::Color color)
{
    switch (color) {
        case Hanabi::RED: return "red";
        case Hanabi::ORANGE: return "orange";
        case Hanabi::YELLOW: return "yellow";
        case Hanabi::GREEN: return "green";
        case Hanabi::BLUE: return "blue";
        case Hanabi::INVALID_COLOR: return "Invalid_color";
    }
    assert(false);
    return "ERROR"; // silence compiler
}

namespace Hanabi {

Card::Card(Color c, Value v): color(c), value(v) { assert(1 <= value && value <= 5); }
Card::Card(Color c, int v): color(c), value(Value(v)) { assert(1 <= value && value <= 5); }

int Card::count() const
{
    switch (value) {
        case 1: return 3;
        case 2: case 3: case 4: return 2;
        case 5: return 1;
        default: HANABI_SERVER_ASSERT(false, "invalid card value");
    }
    return -1; // silence compiler
}

std::string Card::toString() const
{
    std::string result;
    result += (char)('0' + this->value);
    result += (char)(colorname(this->color)[0]);
    return result;
}

Card Pile::topCard() const
{
    HANABI_SERVER_ASSERT(size_ != 0, "empty pile has no top card");
    assert(1 <= size_ && size_ <= 5);
    return Card(color, size_);
}

void Pile::increment_()
{
    assert(0 <= size_ && size_ <= 4);
    ++size_;
}

/* virtual destructor */
Bot::~Bot() { }

/* Hanabi::Card has no default constructor */
Server::Server(): log_(nullptr), activeCard_(RED,1) { }

bool Server::gameOver() const
{
    /* The game ends if there are no more cards to draw... */
    if (this->deck_.empty() && finalCountdown_ == numPlayers_+1) return true;
    /* ...no more mulligans available... */
    if (this->mulligansRemaining_ == 0) return true;
    /* ...or if the piles are all complete. */
    if (this->currentScore() == 5*NUMCOLORS) return true;
    /* Otherwise, the game has not ended. */
    return false;
}

int Server::currentScore() const
{
    if(mulligansRemaining_ == 0 && BOMB0) {
      return 0;
    }

    int sum = 0;
    for (int color = 0; color < NUMCOLORS; ++color) {
        const Pile &pile = this->piles_[color];
        if (!pile.empty()) {
            sum += pile.topCard().value;
        }
    }
    // add a little penalty to discouurage mulligans based on equivalent choices
    if (mulligansRemaining_ == 0) {
      sum = std::max(sum - BOMBD, 0);
    }
    return sum;
}

void Server::setLog(std::ostream *logStream)
{
    this->log_ = logStream;
}

void Server::srand(unsigned int seed)
{
    this->seed_ = seed;
    this->rand_.seed(seed);
}

template<class It, class Gen>
static void portable_shuffle(It first, It last, Gen& g)
{
    const int n = (last - first);
    for (int i=0; i < n; ++i) {
        int j = (g() % (i + 1));
        if (j != i) {
            using std::swap;
            swap(first[i], first[j]);
        }
    }
}

int Server::runGame(const BotFactory &botFactory, int numPlayers)
{
    return this->runGame(botFactory, numPlayers, std::vector<Card>());
}

int Server::runGame(const BotFactory &botFactory, int numPlayers, const std::vector<Card>& stackedDeck)
{
  numPlayers_ = numPlayers;
  std::vector<Bot*> players(numPlayers);
  for (int i=0; i < numPlayers; ++i) {
      players[i] = botFactory.create(i, numPlayers, handSize());
  }
  int score = runGame(players, stackedDeck);
  for (int i=0; i < numPlayers; ++i) {
      botFactory.destroy(players[i]);
  }
  return score;
}

int Server::runGame(std::vector<Bot*> players, const std::vector<Card>& stackedDeck)
{
    std::cerr << "Starting game..." << std::endl;
    /* Create and initialize the bots. */
    players_ = players;
    numPlayers_ = players.size();
    const int initialHandSize = this->handSize();

    /* Initialize the piles and stones. */
    for (Color color = RED; color <= BLUE; ++color) {
        piles_[(int)color].color = color;
        piles_[(int)color].size_ = 0;
    }
    mulligansRemaining_ = NUMMULLIGANS;
    hintStonesRemaining_ = NUMHINTS;
    finalCountdown_ = 0;

    /* Shuffle the deck. */
    if (!stackedDeck.empty()) {
        deck_ = stackedDeck;
        std::reverse(deck_.begin(), deck_.end());  /* because we pull cards from the "top" (back) of the vector */
    } else {
        deck_.clear();
        for (Color color = RED; color <= BLUE; ++color) {
            for (int value = 1; value <= 5; ++value) {
                const Card card(color, value);
                const int n = card.count();
                for (int k=0; k < n; ++k) deck_.push_back(card);
            }
        }
        portable_shuffle(deck_.begin(), deck_.end(), rand_);
    }
#ifdef CARD_ID
    int id_count = 0;
    for (auto &card: deck_) {
      card.id = id_count++;
    }
#endif
    discards_.clear();

    /* Secretly draw the starting hands. */
    hands_.resize(numPlayers_);
    for (int i=0; i < numPlayers_; ++i) {
        hands_[i].clear();
        for (int k=0; k < initialHandSize; ++k) {
            hands_[i].push_back(this->draw_());
        }
    }

    /* Run the game. */
    activeCardIsObservable_ = false;
    activePlayer_ = 0;
    movesFromActivePlayer_ = -1;
    int score = this->runToCompletion();

    return score;
}

int Server::runToCompletion() {
  while (!this->gameOver()) {
    if (log_) {
      *log_ << "====> cards remaining: " << this->cardsRemainingInDeck() << " , empty? " << this->deck_.empty() << " , countdown " << finalCountdown_ << " , mulligans " << this->mulligansRemaining_ << " , score " << this->currentScore() << std::endl;
    }

    if (activePlayer_ == 0) this->logHands_();
    for (int i=0; i < numPlayers_; ++i) {
        observingPlayer_ = i;
        players_[i]->pleaseObserveBeforeMove(*this);
    }
    observingPlayer_ = activePlayer_;
    movesFromActivePlayer_ = 0;
    players_[activePlayer_]->pleaseMakeMove(*this);  /* make a move */
    // added this short-circuit in case you forcibly end the game, toa void asserts and waiting
    if (this->gameOver()) break;
    HANABI_SERVER_ASSERT(movesFromActivePlayer_ != 0, "bot failed to respond to pleaseMove()");
    assert(movesFromActivePlayer_ == 1);
    movesFromActivePlayer_ = -1;
    for (int i=0; i < numPlayers_; ++i) {
        observingPlayer_ = i;
        players_[i]->pleaseObserveAfterMove(*this);
    }
    activePlayer_ = (activePlayer_ + 1) % numPlayers_;
    assert(0 <= finalCountdown_ && finalCountdown_ <= numPlayers_);
    if (deck_.empty()) finalCountdown_ += 1;
  }

  return this->currentScore();
}

void Server::endGameByBombingOut() {
  mulligansRemaining_ = 0;
}

int Server::numPlayers() const
{
    return numPlayers_;
}

int Server::handSize() const
{
    return HAND_SIZE_OVERRIDE >= 0 ? HAND_SIZE_OVERRIDE : ((numPlayers_ <= 3) ? 5 : 4);
}

int Server::whoAmI() const
{
    assert(0 <= observingPlayer_ && observingPlayer_ < numPlayers_);
    return observingPlayer_;
}

int Server::activePlayer() const
{
    return activePlayer_;
}

int Server::sizeOfHandOfPlayer(int player) const
{
    HANABI_SERVER_ASSERT(0 <= player && player < numPlayers_, "player index out of bounds");
    return hands_[player].size();
}

const std::vector<Card>& Server::handOfPlayer(int player) const
{
    HANABI_SERVER_ASSERT(player != observingPlayer_, "cannot observe own hand");
    HANABI_SERVER_ASSERT(0 <= player && player < numPlayers_, "player index out of bounds");
    return hands_[player];
}

const std::vector<int> Server::cardIdsOfHandOfPlayer(int player) const
{
    std::vector<int> res;
    for (auto &card: hands_[player]) {
#ifdef CARD_ID
      res.push_back(card.id);
#else
      res.push_back(0);
#endif
    }
    return res;
}

Card Server::activeCard() const
{
    HANABI_SERVER_ASSERT(activeCardIsObservable_, "called activeCard() from the wrong observer");
    return activeCard_;
}

Pile Server::pileOf(Color color) const
{
    int index = (int)color;
    HANABI_SERVER_ASSERT(0 <= index && index < NUMCOLORS, "invalid Color");
    return piles_[color];
}

const std::vector<Card>& Server::discards() const
{
    return discards_;
}

int Server::hintStonesUsed() const
{
    assert(hintStonesRemaining_ <= NUMHINTS);
    return (NUMHINTS - hintStonesRemaining_);
}

int Server::hintStonesRemaining() const
{
    assert(hintStonesRemaining_ <= NUMHINTS);
    return hintStonesRemaining_;
}

bool Server::discardingIsAllowed() const
{
#ifdef HANABI_ALLOW_DISCARDING_EVEN_WITH_ALL_HINT_STONES
    return true;
#else
    return (hintStonesRemaining_ != NUMHINTS);
#endif
}

int Server::mulligansUsed() const
{
    assert(mulligansRemaining_ <= NUMMULLIGANS);
    return (NUMMULLIGANS - mulligansRemaining_);
}

int Server::mulligansRemaining() const
{
    assert(mulligansRemaining_ <= NUMMULLIGANS);
    return mulligansRemaining_;
}

int Server::cardsRemainingInDeck() const
{
    return deck_.size();
}

int Server::finalCountdown() const
{
  return finalCountdown_;
}

void Server::pleaseDiscard(int index)
{
    assert(0 <= activePlayer_ && activePlayer_ < numPlayers_);
    HANABI_SERVER_ASSERT(movesFromActivePlayer_ < 1, "bot attempted to move twice");
    HANABI_SERVER_ASSERT(movesFromActivePlayer_ == 0, "called pleaseDiscard() from the wrong observer");
    HANABI_SERVER_ASSERT(0 <= index && index <= hands_[activePlayer_].size(), "invalid card index");
    HANABI_SERVER_ASSERT(discardingIsAllowed(), "all hint stones are already available");

    Card discardedCard = hands_[activePlayer_][index];
    activeCard_ = discardedCard;
    activeCardIsObservable_ = true;

    /* Notify all the players of the discard (before it happens). */
    movesFromActivePlayer_ = -1;
    int oldObservingPlayer = observingPlayer_;
    for (int i=0; i < numPlayers_; ++i) {
        observingPlayer_ = i;
        players_[i]->pleaseObserveBeforeDiscard(*this, activePlayer_, index);
    }
    observingPlayer_ = oldObservingPlayer;
    activeCardIsObservable_ = false;

    /* Discard the selected card. */
    discards_.push_back(discardedCard);

    if (log_) {
        (*log_) << "Player " << activePlayer_
                << " discarded his " << nth(index, hands_[activePlayer_].size())
                << " card (" << discardedCard.toString() << ").\n";
    }

    /* Shift the old cards down, and draw a replacement if possible. */
    hands_[activePlayer_].erase(hands_[activePlayer_].begin() + index);

    if (mulligansRemaining_ > 0 && !deck_.empty()) {
        Card replacementCard = this->draw_();
        hands_[activePlayer_].push_back(replacementCard);
        if (log_) {
            (*log_) << "Player " << activePlayer_
                    << " drew a replacement (" << replacementCard.toString() << ").\n";
        }
    }

    regainHintStoneIfPossible_();
    movesFromActivePlayer_ = 1;
}

void Server::pleasePlay(int index)
{
    assert(0 <= activePlayer_ && activePlayer_ < hands_.size());
    assert(players_.size() == hands_.size());
    HANABI_SERVER_ASSERT(movesFromActivePlayer_ < 1, "bot attempted to move twice");
    HANABI_SERVER_ASSERT(movesFromActivePlayer_ == 0, "called pleasePlay() from the wrong observer");
    HANABI_SERVER_ASSERT(0 <= index && index <= hands_[activePlayer_].size(), "invalid card index");

    Card selectedCard = hands_[activePlayer_][index];
    activeCard_ = selectedCard;
    activeCardIsObservable_ = true;

    /* Notify all the players of the attempted play (before it happens). */
    movesFromActivePlayer_ = -1;
    int oldObservingPlayer = observingPlayer_;
    for (int i=0; i < players_.size(); ++i) {
        observingPlayer_ = i;
        players_[i]->pleaseObserveBeforePlay(*this, activePlayer_, index);
    }
    observingPlayer_ = oldObservingPlayer;
    activeCardIsObservable_ = false;

    /* Examine the selected card. */
    Pile &pile = piles_[(int)selectedCard.color];

    if (pile.nextValueIs(selectedCard.value)) {
        if (log_) {
            (*log_) << "Player " << activePlayer_
                    << " played his " << nth(index, hands_[activePlayer_].size())
                    << " card (" << selectedCard.toString() << ").\n";
        }
        pile.increment_();
        if (selectedCard.value == 5) {
            /* Successfully playing a 5 regains a hint stone. */
            regainHintStoneIfPossible_();
        }
    } else {
        /* The card was unplayable! */
        if (log_) {
            (*log_) << "Player " << activePlayer_
                    << " tried to play his " << nth(index, hands_[activePlayer_].size())
                    << " card (" << selectedCard.toString() << ")"
                    << " but failed.\n";
        }
        discards_.push_back(selectedCard);
        loseMulligan_();
    }

    /* Shift the old cards down, and draw a replacement if possible. */
    hands_[activePlayer_].erase(hands_[activePlayer_].begin() + index);

    if (mulligansRemaining_ > 0 && !deck_.empty()) {
        Card replacementCard = this->draw_();
        hands_[activePlayer_].push_back(replacementCard);
        if (log_) {
            (*log_) << "Player " << activePlayer_
                    << " drew a replacement (" << replacementCard.toString() << ").\n";
        }
    }

    this->logPiles_();

    movesFromActivePlayer_ = 1;
}

void Server::pleaseGiveColorHint(int to, Color color)
{
    assert(0 <= activePlayer_ && activePlayer_ < hands_.size());
    assert(players_.size() == hands_.size());
    HANABI_SERVER_ASSERT(movesFromActivePlayer_ < 1, "bot attempted to move twice");
    HANABI_SERVER_ASSERT(movesFromActivePlayer_ == 0, "called pleaseGiveColorHint() from the wrong observer");
    HANABI_SERVER_ASSERT(0 <= to && to < hands_.size(), "invalid player index");
    HANABI_SERVER_ASSERT(RED <= color && color <= BLUE, "invalid color");
    HANABI_SERVER_ASSERT(hintStonesRemaining_ != 0, "no hint stones remaining");
    HANABI_SERVER_ASSERT(to != activePlayer_, "cannot give hint to oneself");

    CardIndices card_indices;
    for (int i=0; i < hands_[to].size(); ++i) {
        if (hands_[to][i].color == color) {
            card_indices.add(i);
        }
    }
#ifndef HANABI_ALLOW_EMPTY_HINTS
    HANABI_SERVER_ASSERT(!card_indices.empty(), "hint must include at least one card");
#endif

    if (log_) {
        const bool singular = (card_indices.size() == 1);
        (*log_) << "Player " << activePlayer_
                << " told player " << to
                << " that ";
        if (card_indices.empty()) {
            (*log_) << "none of his cards were ";
        } else if (card_indices.size() == hands_[to].size()) {
            (*log_) << "his whole hand was ";
        } else {
            (*log_) << "his " << nth(card_indices, hands_[to].size())
                << (singular ? " card was " : " cards were ");
        }
        (*log_) << colorname(color) << ".\n";
    }

    /* Notify all the players of the given hint. */
    movesFromActivePlayer_ = -1;
    int oldObservingPlayer = observingPlayer_;
    for (int i=0; i < players_.size(); ++i) {
        observingPlayer_ = i;
        players_[i]->pleaseObserveColorHint(*this, activePlayer_, to, color, card_indices);
    }
    observingPlayer_ = oldObservingPlayer;

    hintStonesRemaining_ -= 1;
    movesFromActivePlayer_ = 1;
}

void Server::pleaseGiveValueHint(int to, Value value)
{
    assert(0 <= activePlayer_ && activePlayer_ < hands_.size());
    assert(players_.size() == hands_.size());
    HANABI_SERVER_ASSERT(movesFromActivePlayer_ < 1, "bot attempted to move twice");
    HANABI_SERVER_ASSERT(movesFromActivePlayer_ == 0, "called pleaseGiveValueHint() from the wrong observer");
    HANABI_SERVER_ASSERT(0 <= to && to < hands_.size(), "invalid player index");
    HANABI_SERVER_ASSERT(1 <= value && value <= 5, "invalid value");
    HANABI_SERVER_ASSERT(hintStonesRemaining_ != 0, "no hint stones remaining");
    HANABI_SERVER_ASSERT(to != activePlayer_, "cannot give hint to oneself");

    CardIndices card_indices;
    for (int i=0; i < hands_[to].size(); ++i) {
        if (hands_[to][i].value == value) {
            card_indices.add(i);
        }
    }
#ifndef HANABI_ALLOW_EMPTY_HINTS
    HANABI_SERVER_ASSERT(!card_indices.empty(), "hint must include at least one card");
#endif

    if (log_) {
        const bool singular = (card_indices.size() == 1);
        (*log_) << "Player " << activePlayer_
                << " told player " << to
                << " that ";
        if (card_indices.empty()) {
            (*log_) << "none of his cards were ";
        } else if (card_indices.size() == hands_[to].size()) {
            (*log_) << "his whole hand was ";
        } else {
            (*log_) << "his " << nth(card_indices, hands_[to].size())
                << (singular ? " card was " : " cards were ");
        }
        (*log_) << value
                << (singular ? ".\n" : "s.\n");
    }

    /* Notify all the players of the given hint. */
    movesFromActivePlayer_ = -1;
    int oldObservingPlayer = observingPlayer_;
    for (int i=0; i < players_.size(); ++i) {
        observingPlayer_ = i;
        players_[i]->pleaseObserveValueHint(*this, activePlayer_, to, value, card_indices);
    }
    observingPlayer_ = oldObservingPlayer;

    hintStonesRemaining_ -= 1;
    movesFromActivePlayer_ = 1;
}

void Server::regainHintStoneIfPossible_()
{
    if (hintStonesRemaining_ < NUMHINTS) {
        ++hintStonesRemaining_;
        if (log_) {
            (*log_) << "Player " << activePlayer_
                    << " returned a hint stone; there "
                    << ((hintStonesRemaining_ == 1) ? "is" : "are") << " now "
                    << hintStonesRemaining_ << " remaining.\n";
        }
    }
}

void Server::loseMulligan_()
{
    --mulligansRemaining_;
    assert(mulligansRemaining_ >= 0);
    if (log_) {
        if (mulligansRemaining_ == 0) {
            (*log_) << "That was the last mulligan.\n";
        } else if (mulligansRemaining_ == 1) {
            (*log_) << "There is only one mulligan remaining.\n";
        } else {
            (*log_) << "There are " << mulligansRemaining_ << " mulligans remaining.\n";
        }
    }
}

Card Server::draw_()
{
    assert(!deck_.empty());
    Card result = deck_.back();
    deck_.pop_back();
    return result;
}

std::string Server::discardsAsString() const
{
    if (discards_.size() == 0) {
      return "";
    }
    std::ostringstream oss;
    for (const Card &card : discards_) {
        oss << ' ' << card.toString();
    }
    return oss.str().substr(1);
}

std::string Server::handsAsString() const
{
    std::ostringstream oss;
    for (int i=0; i < numPlayers_; ++i) {
        for (int j=0; j < (int)hands_[i].size(); ++j) {
            oss << (j ? ',' : ' ') << hands_[i][j].toString();
        }
    }
    return oss.str().substr(1);
}

std::string Server::pilesAsString() const
{
    std::ostringstream oss;
    for (Color k = RED; k <= BLUE; ++k) {
        oss << ' ' << piles_[k].size() << Card(k, 1).toString()[1];
    }
    return oss.str().substr(1);
}

std::vector<Card> Server::cheatGetHand(int index) const
{
  return hands_[index];
}

void Server::logHands_() const
{
    if (log_) {
        (*log_) << "Current hands:";
        for (int i=0; i < numPlayers_; ++i) {
            for (int j=0; j < (int)hands_[i].size(); ++j) {
                (*log_) << (j ? "," : " ") << hands_[i][j].toString();
            }
        }
        (*log_) << "\n";
    }
}

void Server::logPiles_() const
{
    if (log_) {
        (*log_) << "Current piles:";
        for (Color k = RED; k <= BLUE; ++k) {
            (*log_) << " " << piles_[k].size() << Card(k, 1).toString()[1];
        }
        (*log_) << "\n";
    }
}

std::map<std::string, std::shared_ptr<BotFactory>> &getBotFactoryMap() {
  static std::map<std::string, std::shared_ptr<BotFactory>> factoryMap;
  return factoryMap;
}

void registerBotFactory(std::string name, std::shared_ptr<Hanabi::BotFactory> factory) {
  getBotFactoryMap()[name] = factory;
  std::cerr << "Registered " << name << std::endl;
}

std::shared_ptr<Hanabi::BotFactory> getBotFactory(const std::string &botName) {
  if (getBotFactoryMap().count(botName) == 0) {
    throw std::runtime_error("Unknown bot: " + botName);
  }
  return getBotFactoryMap().at(botName);
}


}  /* namespace Hanabi */
