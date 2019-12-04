//  Copyright (c) Facebook, Inc. and its affiliates.
//  All rights reserved.
//
//  This source code is licensed under the license found in the
//  LICENSE file in the root directory of this source tree.

import React from "react";
import ShowHints from "./ShowHints";
import { BoardContext } from "../BoardState";
import CardLeftCount from "./CardLeftCount";
import { Flipper, Flipped } from "react-flip-toolkit";
import cx from "classnames";

function PlayerHand() {
  const {
    gameState: state,
    state: { colors },
    actions
  } = React.useContext(BoardContext);

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

  const player = state.partnerId;

  return (
    <div className={cx("area", { active: !state.isPlayerTurn })}>
      <h3>Player {["A", "B"][player]}</h3>
      <div>
        <Flipper
          flipKey={state.hands["player" + player]
            .map(card => card.id)
            .join(",")}
          className="hand"
        >
          {state.hands["player" + player].map((card, idx) => {
            const [cardColor, cardNumber] = card.card.split("");

            return (
              <Flipped
                key={card.id}
                flipId={card.id}
                onAppear={onAppear}
                onExit={onExit}
              >
                <div
                  key={card + "," + idx}
                  className={"card " + card.card.split("")[0]}
                >
                  <div className="actions">
                    <button
                      disabled={state.hintCounter === 0 || !state.isPlayerTurn}
                      onClick={() => {
                        actions.hintColor(
                          colors.findIndex(
                            color => color[0] === card.card.split("")[0]
                          ),
                          player
                        );
                      }}
                    >
                      Hint Color
                    </button>
                    <button
                      disabled={state.hintCounter === 0 || !state.isPlayerTurn}
                      onClick={() => {
                        actions.hintNumber(card.card.split("")[1], player);
                      }}
                    >
                      Hint Number
                    </button>
                  </div>
                  <div className="card_desc">
                    {card.card}
                    <div
                      style={{
                        fontSize: 12,
                        display: "inline-block",
                        float: "right",
                        padding: 5
                      }}
                    >
                      <CardLeftCount color={cardColor} number={cardNumber} />
                    </div>
                  </div>
                  <ShowHints hints={card.hints} />
                </div>
              </Flipped>
            );
          })}
        </Flipper>
      </div>
    </div>
  );
}

export default PlayerHand;
