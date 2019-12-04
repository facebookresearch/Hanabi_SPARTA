//  Copyright (c) Facebook, Inc. and its affiliates.
//  All rights reserved.
//
//  This source code is licensed under the license found in the
//  LICENSE file in the root directory of this source tree.

import React from "react";
import { BoardContext } from "../BoardState";
import cx from "classnames";

function CardLeftCount({ color, number }) {
  const { gameState: state } = React.useContext(BoardContext);

  // const discarded = state.discarded.filter(card => card === color + "" + number)
  //   .length;

  // const startCount = {
  //   1: 3,
  //   2: 2,
  //   3: 2,
  //   4: 2,
  //   5: 1
  // };

  // const cardsLeft = startCount[number] - discarded;
  const cardsLeft = state.deckComposition[number + "" + color.toLowerCase()];

  const colorCount = Object.entries(state.placed)
    .map(([fullColor, count]) => [fullColor[0], count])
    .find(([colorLetter, _]) => colorLetter === color)[1];

  return (
    <span
      className={cx("cards-left", {
        none: cardsLeft === 0,
        one: cardsLeft === 1,
        two: cardsLeft === 2,
        played: number <= colorCount
      })}
    >
      x{cardsLeft}
    </span>
  );
}

export default CardLeftCount;
