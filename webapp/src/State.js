//  Copyright (c) Facebook, Inc. and its affiliates.
//  All rights reserved.
//
//  This source code is licensed under the license found in the
//  LICENSE file in the root directory of this source tree.

const state = {
  discarded: ["G5"],
  hands: {
    self: [
      { hints: ["Y", "1"] },
      { hints: ["Y"] },
      { hints: ["2"] },
      { hints: ["2"] },
      { hints: [] }
    ],
    player1: [
      { card: "Y1", hints: ["Y", "1"] },
      { card: "W2", hints: ["W"] },
      { card: "R2", hints: ["2"] },
      { card: "G2", hints: ["2"] },
      { card: "B5", hints: [] }
    ]
  },
  placed: {
    white: 0,
    red: 0,
    blue: 0,
    yellow: 0,
    green: 0
  },
  hintsLog: [],
  defuseCounter: 3,
  hintCounter: 8,
  cardsLeft: 30
};

export { state, convertState };

function convertCard(card) {
  try {
    const [value, color] = card.toUpperCase().split("");
    return color + value;
  } catch {
    console.log(`ERROR converting: ${card}`);
  }
}

function convertState(schema) {
  if (!schema.isConnected) return {};
  return {
    isPlayerTurn: schema.isPlayerTurn,
    discarded: schema.discards.map(convertCard),
    hands: {
      self: new Array(5).fill(1).map((_, idx) => ({
        id: schema.cardsIds[idx],
        card: convertCard(schema.cheatMyCards[idx]),
        hints: schema.predictions[schema.playerId][idx]
      })),
      ["player" + schema.partnerId]: schema.cards.map((card, idx) => ({
        id: schema.partnerCardsIds[idx],
        card: convertCard(card),
        hints: schema.predictions[schema.partnerId][idx]
      }))
    },
    placed: Object.fromEntries(
      new Map(
        new Array(5)
          .fill(1)
          .map((_, idx) => [schema.colors[idx], schema.piles[idx]])
      )
    ),
    deckComposition: schema.deckComposition,
    hintsLog: schema.history,
    defuseCounter: schema.mulligansRemaining,
    hintCounter: schema.hintStonesRemaining,
    cardsLeft: schema.cardsRemainingInDeck,
    gameOver: schema.gameOver,
    selfId: schema.playerId,
    partnerId: schema.partnerId,
    botName: schema.botName,
    otherBots: schema.otherBots,
    seed: schema.seed,
    history: schema.moveHistory
  };
}
