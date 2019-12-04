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

function RevealOwnHand() {
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
  console.log(state.hands["self"]);

  return (
    <div className={cx("area", { active: state.isPlayerTurn })}>
      <h3>You</h3>
      <div>
        <Flipper
          flipKey={state.hands["self"].map(card => card.id).join(",")}
          className="hand"
        >
          {state.hands["self"]
            .filter(card => !!card.card)
            .map((card, idx) => {
              // const [cardColor, cardNumber] = card.card.split("");

              return (
                <Flipped
                  key={card.id}
                  flipId={card.id}
                  onAppear={onAppear}
                  onExit={onExit}
                >
                  <div key={idx} className={"card " + card.card.split("")[0]}>
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
                        {/* <CardLeftCount color={cardColor} number={cardNumber} /> */}
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

export default RevealOwnHand;
