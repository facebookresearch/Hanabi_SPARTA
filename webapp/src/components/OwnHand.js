//  Copyright (c) Facebook, Inc. and its affiliates.
//  All rights reserved.
//
//  This source code is licensed under the license found in the
//  LICENSE file in the root directory of this source tree.

import React from "react";
import ShowHints from "./ShowHints";
import { BoardContext } from "../BoardState";
import { Flipper, Flipped } from "react-flip-toolkit";
import cx from "classnames";

function OwnHand() {
  const {
    gameState: state,
    state: { colors, notes, isPlayerTurn },
    actions
  } = React.useContext(BoardContext);

  const lastMove = state.hintsLog[state.hintsLog.length - 1];
  const lastMoveWasHint =
    lastMove.idx &&
    (lastMove.type === "HINT_VALUE" || lastMove.type === "HINT_COLOR");
  const hintedValue =
    lastMove.type === "HINT_VALUE"
      ? lastMove.value
      : lastMove.type === "HINT_COLOR"
      ? colors[lastMove.value]
      : null;
  const plural = hintedValue !== null ? lastMove.idx.length !== 1 : null;
  const message =
    '"' +
    (plural ? `These ${lastMove.idx.length} cards are ` : "This card is ") +
    (lastMove.type === "HINT_VALUE" ? `a ${hintedValue}` : hintedValue) +
    '"';

  function onAppear(el, i) {
    setTimeout(() => {
      el.classList.add("fadeIn");
      setTimeout(() => {
        el.style.opacity = 1;
        el.classList.remove("fadeIn");
      }, 500);
    }, i * 50);
  }

  function onExit(el, i, removeElement) {
    setTimeout(() => {
      el.classList.add("fadeOut");
      setTimeout(removeElement, 500);
    }, i * 50);
  }

  return (
    <div className={cx("area own", { active: state.isPlayerTurn })}>
      <h3>You</h3>
      <div>
        <Flipper
          flipKey={state.hands.self.map(card => card.id).join(",")}
          className="hand"
        >
          {state.hands.self.map((card, idx) => (
            <Flipped
              key={card.id}
              flipId={card.id}
              onAppear={onAppear}
              onExit={onExit}
            >
              <div
                key={idx}
                className={cx("card ", "hidden", {
                  hinted:
                    lastMoveWasHint &&
                    lastMove.idx.indexOf(idx) >= 0 &&
                    lastMove.player === state.selfId
                })}
              >
                <div className="actions">
                  <button
                    disabled={!state.isPlayerTurn || !isPlayerTurn}
                    onClick={() => {
                      actions.place(idx);
                    }}
                  >
                    Play
                  </button>
                  <button
                    style={{ backgroundColor: "#94001c" }}
                    disabled={state.hintCounter === 8 || !isPlayerTurn}
                    onClick={() => {
                      actions.discard(idx);
                    }}
                  >
                    Discard
                  </button>
                </div>
                <div className="card_notes_wrapper">
                  <textarea
                    key={card.id}
                    className="card_notes"
                    placeholder="Type notes..."
                    value={notes[card.id]}
                    onChange={e => actions.updateNote(card.id, e.target.value)}
                    row={2}
                  ></textarea>
                </div>
                <ShowHints hints={card.hints} />
              </div>
            </Flipped>
          ))}
        </Flipper>
        <span style={{ marginLeft: 20 }}>
          {state.isPlayerTurn && hintedValue ? message : null}
        </span>
      </div>
    </div>
  );
}

export default OwnHand;
