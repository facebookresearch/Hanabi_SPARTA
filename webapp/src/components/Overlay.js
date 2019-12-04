//  Copyright (c) Facebook, Inc. and its affiliates.
//  All rights reserved.
//
//  This source code is licensed under the license found in the
//  LICENSE file in the root directory of this source tree.

import React from "react";
import { BoardContext } from "../BoardState";

function Overlay() {
  const {
    gameState: { botName, otherBots },
    actions
  } = React.useContext(BoardContext);

  const [seed, setSeed] = React.useState(null);

  return (
    <div
      style={{
        position: "fixed",
        top: 0,
        right: 0,
        left: 0,
        bottom: 0,
        backgroundColor: "rgba(0,0,0,0.6)",
        color: "white",
        fontSize: 40,
        zIndex: 99999,
        display: "flex",
        justifyContent: "center",
        alignItems: "center",
        flexDirection: "column"
      }}
    >
      <button
        onClick={() => actions.playAgain(seed, botName)}
        className="connect"
        style={{ fontSize: otherBots.length === 0 ? null : 14, margin: "10px" }}
      >
        {/* Play Again with {botName} */}
        Play Again
      </button>
      <div>
        {otherBots.map(otherName => (
          <button
            key={otherName}
            onClick={() => actions.playAgain(seed, otherName)}
            className="connect"
            style={{ margin: "10px" }}
          >
            Play with {otherName}
          </button>
        ))}
      </div>
      <input
        className="seed-input"
        value={seed}
        onChange={e => setSeed(e.target.value)}
        placeholder={"Optional start seed..."}
      />
    </div>
  );
}

export default Overlay;
