//  Copyright (c) Facebook, Inc. and its affiliates.
//  All rights reserved.
//
//  This source code is licensed under the license found in the
//  LICENSE file in the root directory of this source tree.

import React from "react";
import CardLeftCount from "./CardLeftCount";
import { BoardContext } from "../BoardState";
import cx from "classnames";
import { Flipper, Flipped } from "react-flip-toolkit";

function GameBoard() {
  const {
    gameState: state,
    state: { colors }
  } = React.useContext(BoardContext);

  function onAppear(el, i) {
    setTimeout(() => {
      el.classList.add("fadeUp");
      setTimeout(() => {
        el.style.opacity = 1;
        el.classList.remove("fadeUp");
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
    <div className="area horizontal">
      <div className="cards_overview">
        <h3>Cards Left &mdash; {state.cardsLeft}</h3>
        <table>
          <thead>
            <tr>
              <th></th>
              <th>1</th>
              <th>2</th>
              <th>3</th>
              <th>4</th>
              <th>5</th>
            </tr>
          </thead>
          <tbody>
            {colors
              .map(color => color[0])
              .map(color => (
                <tr key={color}>
                  <td className="row-header">{color}</td>
                  {new Array(5).fill(1).map((_, idx) => (
                    <td key={idx}>
                      <CardLeftCount color={color} number={idx + 1} />
                    </td>
                  ))}
                </tr>
              ))}
          </tbody>
        </table>
      </div>
      <div className="board_cards">
        <h3>
          Fireworks &mdash; Score:{" "}
          {Object.values(state.placed).reduce((a, b) => a + b)}
        </h3>

        <div className="hand">
          {colors.map(color => (
            <div
              key={color}
              className={cx(
                "card",
                { empty: state.placed[color] === 0 },
                color[0]
              )}
            >
              <div className="card_desc">
                {state.placed[color] === 0
                  ? null
                  : color[0] + "" + state.placed[color]}
              </div>
            </div>
          ))}
        </div>
      </div>

      <div className="discards_pile">
        <h3>Discard Pile ({state.discarded.length})</h3>
        <div>
          <Flipper flipKey={state.discarded.length} className="pile">
            {state.discarded.length === 0 ? (
              <div className="card empty" key="empty"></div>
            ) : null}
            {state.discarded.length > 3 ? (
              <div className="card hidden" key="pile"></div>
            ) : null}
            {state.discarded.map((card, idx, arr) => {
              if (arr.length > 3 && idx < arr.length - 3) return null;
              return (
                <Flipped
                  key={idx + "" + card}
                  flipId={idx + "" + card}
                  onAppear={onAppear}
                  // onExit={onExit}
                >
                  <div
                    key={idx + "" + card}
                    className={cx("card", "discarded", card[0])}
                  >
                    <div className="card_desc">{card}</div>
                  </div>
                </Flipped>
              );
            })}
          </Flipper>
        </div>
      </div>
    </div>
  );
}

export default GameBoard;
