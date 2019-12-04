//  Copyright (c) Facebook, Inc. and its affiliates.
//  All rights reserved.
//
//  This source code is licensed under the license found in the
//  LICENSE file in the root directory of this source tree.

import React from "react";
import { BoardContext } from "./BoardState";
import Overlay from "./components/Overlay";
import History from "./components/History";
import OwnHand from "./components/OwnHand";
import RevealOwnHand from "./components/RevealOwnHand";
import PlayerHand from "./components/PlayerHand";
import GameBoard from "./components/GameBoard";
import Counters from "./components/Counters";
import { StickyContainer } from "react-sticky";

function App() {
  const { gameState: state } = React.useContext(BoardContext);

  return (
    <div>
      {state.gameOver && <Overlay>Game Over!</Overlay>}
      <History />
      <h1>
        <span role="img" aria-label="logo">
          🎆
        </span>{" "}
        Hanabi{" "}
        <span className="tiny-text">
          {state.botName} &mdash; Seed: {state.seed}
        </span>
      </h1>
      <StickyContainer>
        <Counters />
        <GameBoard />
        <PlayerHand />
        {state.gameOver === true ? <RevealOwnHand /> : <OwnHand />}
      </StickyContainer>
    </div>
  );
}

export default App;
