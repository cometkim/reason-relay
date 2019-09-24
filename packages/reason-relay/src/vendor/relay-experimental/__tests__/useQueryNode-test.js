/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * @emails oncall+relay
 * 
 * @format
 */
'use strict';

var _interopRequireDefault = require("@babel/runtime/helpers/interopRequireDefault");

var _assertThisInitialized2 = _interopRequireDefault(require("@babel/runtime/helpers/assertThisInitialized"));

var _inheritsLoose2 = _interopRequireDefault(require("@babel/runtime/helpers/inheritsLoose"));

var _defineProperty2 = _interopRequireDefault(require("@babel/runtime/helpers/defineProperty"));

var React = require('react');

var ReactTestRenderer = require('react-test-renderer');

var RelayEnvironmentProvider = require('../RelayEnvironmentProvider');

var useQueryNode = require('../useQueryNode');

var _require = require('relay-runtime'),
    createOperationDescriptor = _require.createOperationDescriptor;

var defaultFetchPolicy = 'network-only';

function expectToBeRendered(renderFn, readyState) {
  // Ensure useEffect is called before other timers
  ReactTestRenderer.act(function () {
    jest.runAllImmediates();
  });
  expect(renderFn).toBeCalledTimes(1);
  expect(renderFn.mock.calls[0][0]).toEqual(readyState);
  renderFn.mockClear();
}

function expectToBeFetched(environment, node, variables) {
  expect(environment.execute).toBeCalledTimes(1);
  expect(environment.execute.mock.calls[0][0].operation).toMatchObject({
    fragment: expect.anything(),
    root: expect.anything(),
    request: {
      node: node,
      variables: variables
    }
  });
}

describe('useQueryNode', function () {
  var environment;
  var gqlQuery;
  var renderFn;
  var render;
  var createMockEnvironment;
  var generateAndCompile;
  var Container;
  var setProps;
  beforeEach(function () {
    jest.resetModules();
    jest.spyOn(console, 'warn').mockImplementationOnce(function () {});
    jest.mock('fbjs/lib/ExecutionEnvironment', function () {
      return {
        canUseDOM: function canUseDOM() {
          return true;
        }
      };
    });

    var _require2 = require('relay-test-utils-internal');

    createMockEnvironment = _require2.createMockEnvironment;
    generateAndCompile = _require2.generateAndCompile;

    var ErrorBoundary =
    /*#__PURE__*/
    function (_React$Component) {
      (0, _inheritsLoose2["default"])(ErrorBoundary, _React$Component);

      function ErrorBoundary() {
        var _this;

        for (var _len = arguments.length, args = new Array(_len), _key = 0; _key < _len; _key++) {
          args[_key] = arguments[_key];
        }

        _this = _React$Component.call.apply(_React$Component, [this].concat(args)) || this;
        (0, _defineProperty2["default"])((0, _assertThisInitialized2["default"])(_this), "state", {
          error: null
        });
        return _this;
      }

      var _proto = ErrorBoundary.prototype;

      _proto.componentDidCatch = function componentDidCatch(error) {
        this.setState({
          error: error
        });
      };

      _proto.render = function render() {
        var _this$props = this.props,
            children = _this$props.children,
            fallback = _this$props.fallback;
        var error = this.state.error;

        if (error) {
          return React.createElement(fallback, {
            error: error
          });
        }

        return children;
      };

      return ErrorBoundary;
    }(React.Component);

    var Renderer = function Renderer(props) {
      var query = createOperationDescriptor(gqlQuery, props.variables);
      var data = useQueryNode({
        query: query,
        fetchPolicy: props.fetchPolicy || defaultFetchPolicy,
        componentDisplayName: 'TestDisplayName'
      });
      return renderFn(data);
    };

    Container = function Container(props) {
      var _React$useState = React.useState(props),
          nextProps = _React$useState[0],
          setNextProps = _React$useState[1];

      setProps = setNextProps;
      return React.createElement(Renderer, nextProps);
    };

    render = function render(environment, children) {
      return ReactTestRenderer.create(React.createElement(RelayEnvironmentProvider, {
        environment: environment
      }, React.createElement(ErrorBoundary, {
        fallback: function fallback(_ref2) {
          var error = _ref2.error;
          return "Error: ".concat(error.message + ': ' + error.stack);
        }
      }, React.createElement(React.Suspense, {
        fallback: "Fallback"
      }, children))));
    };

    environment = createMockEnvironment();
    var generated = generateAndCompile("\n      fragment UserFragment on User {\n        name\n      }\n\n      query UserQuery($id: ID) {\n        node(id: $id) {\n          id\n          name\n          ...UserFragment\n        }\n      }\n    ");
    gqlQuery = generated.UserQuery;
    renderFn = jest.fn(function (result) {
      var _ref, _result$node;

      return (_ref = result === null || result === void 0 ? void 0 : (_result$node = result.node) === null || _result$node === void 0 ? void 0 : _result$node.name) !== null && _ref !== void 0 ? _ref : 'Empty';
    });
  });
  afterEach(function () {
    environment.mockClear();
    jest.clearAllTimers();
  });
  it('fetches and renders the query data', function () {
    var variables = {
      id: '1'
    };
    var instance = render(environment, React.createElement(Container, {
      variables: variables
    }));
    var operation = createOperationDescriptor(gqlQuery, variables);
    expect(instance.toJSON()).toEqual('Fallback');
    expectToBeFetched(environment, gqlQuery, variables);
    expect(renderFn).not.toBeCalled();
    expect(environment.retain).toHaveBeenCalledTimes(1);
    environment.mock.resolve(gqlQuery, {
      data: {
        node: {
          __typename: 'User',
          id: variables.id,
          name: 'Alice'
        }
      }
    });
    var data = environment.lookup(operation.fragment).data;
    expectToBeRendered(renderFn, data);
  });
  it('fetches and renders correctly if previously useEffect does not run', function () {
    var variables = {
      id: '1'
    };
    var operation = createOperationDescriptor(gqlQuery, variables);
    var payload = {
      data: {
        node: {
          __typename: 'User',
          id: variables.id,
          name: 'Alice'
        }
      }
    };
    var instance = render(environment, React.createElement(Container, {
      variables: variables
    }));
    expect(instance.toJSON()).toEqual('Fallback');
    expectToBeFetched(environment, gqlQuery, variables);
    expect(renderFn).not.toBeCalled();
    expect(environment.retain).toHaveBeenCalledTimes(1);
    ReactTestRenderer.act(function () {
      environment.mock.resolve(gqlQuery, payload);
    }); // Unmount the component before it gets to permanently retain the data

    instance.unmount();
    expect(renderFn).not.toBeCalled(); // Running all immediates makes sure all useEffects run and GC isn't
    // Triggered by mistake

    ReactTestRenderer.act(function () {
      return jest.runAllImmediates();
    }); // Trigger timeout and GC to clear all references

    ReactTestRenderer.act(function () {
      return jest.runAllTimers();
    }); // Verify GC has run

    expect(environment.getStore().getSource().toJSON()).toEqual({});
    renderFn.mockClear();
    environment.retain.mockClear();
    environment.execute.mockClear();
    instance = render(environment, React.createElement(Container, {
      variables: variables
    }));
    expect(instance.toJSON()).toEqual('Fallback');
    expectToBeFetched(environment, gqlQuery, variables);
    expect(renderFn).not.toBeCalled();
    expect(environment.retain).toHaveBeenCalledTimes(1);
    ReactTestRenderer.act(function () {
      environment.mock.resolve(gqlQuery, payload);
    });
    var data = environment.lookup(operation.fragment).data;
    expectToBeRendered(renderFn, data);
  });
  it('fetches and renders correctly if the same query was unsubscribed before', function () {
    // Render the component
    var initialDescriptor = createOperationDescriptor(gqlQuery, {
      id: 'first-render'
    });
    environment.commitPayload(initialDescriptor, {
      node: {
        __typename: 'User',
        id: 'first-render',
        name: 'Bob'
      }
    });
    var instance = render(environment, React.createElement(Container, {
      variables: {
        id: 'first-render'
      },
      fetchPolicy: "store-only"
    }));
    expect(instance.toJSON()).toEqual('Bob');
    renderFn.mockClear(); // Suspend on the first query

    var variables = {
      id: '1'
    };
    ReactTestRenderer.act(function () {
      setProps({
        variables: variables
      });
    });
    expect(instance.toJSON()).toEqual('Fallback');
    expectToBeFetched(environment, gqlQuery, variables);
    expect(renderFn).not.toBeCalled();
    renderFn.mockClear();
    environment.retain.mockClear();
    environment.execute.mockClear(); // Switch to the second query to cancel the first query

    var nextVariables = {
      id: '2'
    };
    ReactTestRenderer.act(function () {
      setProps({
        variables: nextVariables
      });
    });
    expect(instance.toJSON()).toEqual('Fallback');
    expectToBeFetched(environment, gqlQuery, nextVariables);
    expect(renderFn).not.toBeCalled();
    expect(environment.retain).toHaveBeenCalledTimes(1);
    renderFn.mockClear();
    environment.retain.mockClear();
    environment.execute.mockClear(); // Switch back to the first query and it should request again

    ReactTestRenderer.act(function () {
      setProps({
        variables: variables
      });
    });
    expect(instance.toJSON()).toEqual('Fallback');
    expectToBeFetched(environment, gqlQuery, variables);
    expect(renderFn).not.toBeCalled();
    expect(environment.retain).toHaveBeenCalledTimes(1);
    var operation = createOperationDescriptor(gqlQuery, variables);
    var payload = {
      data: {
        node: {
          __typename: 'User',
          id: variables.id,
          name: 'Alice'
        }
      }
    };
    ReactTestRenderer.act(function () {
      environment.mock.resolve(gqlQuery, payload);
      jest.runAllImmediates();
    });
    var data = environment.lookup(operation.fragment).data;
    expect(renderFn.mock.calls[0][0]).toEqual(data);
    expect(instance.toJSON()).toEqual('Alice');
  });
});