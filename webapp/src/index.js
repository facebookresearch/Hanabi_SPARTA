//  Copyright (c) Facebook, Inc. and its affiliates.
//  All rights reserved.
//
//  This source code is licensed under the license found in the
//  LICENSE file in the root directory of this source tree.

import React from "react";
import ReactDOM from "react-dom";
import App from "./App";
import "./styles.css";
import { BoardState } from "./BoardState";
import LoadingScreen from "./components/LoadingScreen";

ReactDOM.render(
  <BoardState disconnected={LoadingScreen}>
    <App />
  </BoardState>,
  document.getElementById("root")
);
