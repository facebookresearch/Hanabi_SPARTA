//  Copyright (c) Facebook, Inc. and its affiliates.
//  All rights reserved.
//
//  This source code is licensed under the license found in the
//  LICENSE file in the root directory of this source tree.

import React from "react";
import { convertState } from "./State";
import { throttle } from "./utils";

const initialState = {
  isConnected: false,
  connectionMessage: "",
  history: [],
  notes: {},
  gameId: 0
};

const BoardContext = React.createContext(initialState);

const reducerFn = (state, action) => {
  switch (action.type) {
    case "PLAY_AGAIN":
      return initialState;
    case "ACTION_SENT":
      return {
        ...state,
        isPlayerTurn: false
      };
    case "START":
      return state;
    // return { ...state, isConnected: true };
    case "INIT":
      return {
        history: [],
        notes: {},
        gameId: state.gameId + 1,
        colors: action.payload.colors
      };
    case "SERVER_ERROR":
      return {
        connected: false,
        message: action.errorMessage || "An error occured."
      };
    case "UPDATE":
      const {
        hintPlayer,
        hintValue,
        hintType,
        hintIdx,
        ...payload
      } = action.payload;
      return {
        ...state,
        ...payload,
        isConnected: true,
        history: [
          ...state.history,
          {
            player: hintPlayer,
            value: hintValue,
            type: hintType,
            idx: hintIdx
          }
        ]
      };
    case "MOVE":
      return {
        ...state,
        history: [...state.history, action.payload]
      };
    case "UPDATE_NOTE":
      return {
        ...state,
        notes: {
          ...state.notes,
          [action.payload.cardId]: action.payload.noteText
        }
      };
    default:
      return state;
  }
};

function BoardState({ children, disconnected: Disconnected }) {
  const [state, dispatch] = React.useReducer(reducerFn, initialState);
  const websocket = React.useRef();
  const { isConnected } = state;

  const currentPlayerId = state.playerId;
  const actions = {
    place: function(idx) {
      dispatch({ type: "ACTION_SENT" });
      websocket.current.send("PLACE " + idx);
    },
    discard: function(idx) {
      dispatch({ type: "ACTION_SENT" });
      websocket.current.send("DISCARD " + idx);
    },
    hintColor: function(idx, player) {
      dispatch({ type: "ACTION_SENT" });
      websocket.current.send("HINT_COLOR " + idx + " " + player);
    },
    hintNumber: function(idx, player) {
      dispatch({ type: "ACTION_SENT" });
      websocket.current.send("HINT_VALUE " + idx + " " + player);
    },
    updateNote: function(cardId, noteText) {
      dispatch({ type: "UPDATE_NOTE", payload: { cardId, noteText } });
    },
    playAgain: function(seed, botName) {
      if (seed) {
        websocket.current.send(`REPLAY ${seed} ${botName.replace(" ", "-")}`);
      } else {
        websocket.current.send(`REPLAY - ${botName.replace(" ", "-")}`);
      }
    }
  };

  const tryConnect = () => {
    try {
      websocket.current = new WebSocket(
        "ws://" + window.location.hostname + ":5000/connect"
      );
    } catch {
      dispatch({
        type: "SERVER_ERROR",
        errorMessage:
          "Could not connect to the server. Perhaps there are no games running..."
      });
    }

    websocket.current.onopen = function() {
      dispatch({ type: "START" });
    };
    websocket.current.onclose = function() {
      dispatch({
        type: "SERVER_ERROR",
        errorMessage: "The game has ended."
      });
    };
    websocket.current.onerror = function() {
      dispatch({
        type: "SERVER_ERROR",
        errorMessage:
          "Could not connect to the server. Perhaps there are no games running..."
      });
    };
    const throttledDispatch = throttle(dispatch, 1000);
    websocket.current.onmessage = function(msg) {
      const data = JSON.parse(msg.data);
      if (data.type === "INIT") {
        dispatch({ type: "INIT", payload: data });
      } else {
        throttledDispatch({ type: "UPDATE", payload: data });
      }

      console.log("ws rcv", data);
    };
  };

  return (
    <BoardContext.Provider
      value={{ state, gameState: convertState(state), dispatch, actions }}
    >
      {isConnected ? (
        children
      ) : (
        <Disconnected {...{ tryConnect, message: state.connectionMessage }} />
      )}
    </BoardContext.Provider>
  );
}

export { BoardState, BoardContext };
