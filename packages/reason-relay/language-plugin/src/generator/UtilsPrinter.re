open Types;

let printConnectionTraverser =
    (
      ~connectionPropNullable,
      ~edgesPropNullable,
      ~edgesNullable,
      ~nodeNullable,
    ) => {
  let str = ref("");
  let addToStr = s => str := str^ ++ s;
  let strEnd = ref("");
  let addToStrEnd = s => strEnd := s ++ strEnd^;

  if (connectionPropNullable) {
    addToStr("switch(connection) { ");
    addToStr("| None => [||] ");
    addToStr("| Some(connection) => ");
    addToStrEnd("}");
  };

  if (edgesPropNullable) {
    addToStr("switch(connection.edges) { ");
    addToStr("| None => [||] ");
    addToStr("| Some(edges) => edges");
    addToStrEnd("}");
  } else {
    addToStr("connection.edges");
  };

  if (edgesNullable) {
    addToStr("->Belt.Array.keepMap(edge => switch(edge) { ");
    addToStr("| None => None ");
    addToStr("| Some(edge) => ");
    addToStrEnd("})");
  } else {
    addToStr("->Belt.Array.keepMap(edge => ");
    addToStrEnd(")");
  };

  if (nodeNullable) {
    addToStr("switch(edge.node) { ");
    addToStr("| None => None ");
    addToStr("| Some(node) => Some(node)");
    addToStrEnd("}");
  } else {
    addToStr("Some(edge.node)");
  };

  str^ ++ strEnd^;
};

let printFullGetConnectionNodesFnDefinition =
    (
      ~connectionLocation,
      ~connectionPropNullable,
      ~connectionTypeName,
      ~nodeTypeName,
      ~edgesPropNullable,
      ~edgesNullable,
      ~nodeNullable,
    ) =>
  "let getConnectionNodes_"
  ++ connectionLocation
  ++ ": "
  ++ (connectionPropNullable ? "option(" : "")
  ++ connectionTypeName
  ++ (connectionPropNullable ? ")" : "")
  ++ " => array("
  ++ nodeTypeName
  ++ ") = connection => "
  ++ printConnectionTraverser(
       ~connectionPropNullable,
       ~edgesPropNullable,
       ~edgesNullable,
       ~nodeNullable,
     )
  ++ ";";

let printGetConnectionNodesFunction =
    (~state: fullState, ~connectionLocation: string, obj: object_): string => {
  let str = ref("");
  let addToStr = s => str := str^ ++ s;

  obj.values
  |> Tablecloth.Array.forEach(~f=v =>
       switch (v) {
       | Prop(
           name,
           {nullable, propType: Object({values, atPath: connectionAtPath})},
         )
           when name == connectionLocation =>
         let connectionPropNullable = nullable;

         values
         |> Tablecloth.Array.forEach(~f=v =>
              switch (v) {
              | Prop(
                  "edges",
                  {
                    nullable,
                    propType:
                      Array({
                        nullable: arrayNullable,
                        propType: Object({values}),
                      }),
                  },
                ) =>
                let edgesPropNullable = nullable;
                let edgesNullable = arrayNullable;

                values
                |> Tablecloth.Array.forEach(~f=v =>
                     switch (v) {
                     | Prop(
                         "node",
                         {
                           nullable,
                           propType:
                             Object({atPath: nodeAtPath}) |
                             Union({atPath: nodeAtPath}),
                         },
                       ) =>
                       let nodeNullable = nullable;

                       switch (
                         state.objects
                         |> Tablecloth.List.find(~f=o =>
                              o.atPath == nodeAtPath
                            ),
                         state.unions
                         |> Tablecloth.List.find(~f=(u: Types.union) =>
                              u.atPath == nodeAtPath
                            ),
                         state.objects
                         |> Tablecloth.List.find(~f=o =>
                              o.atPath == connectionAtPath
                            ),
                       ) {
                       | (
                           Some({recordName: Some(nodeTypeName)}),
                           None,
                           Some({recordName: Some(connectionTypeName)}),
                         ) =>
                         addToStr(
                           printFullGetConnectionNodesFnDefinition(
                             ~connectionLocation,
                             ~connectionPropNullable,
                             ~connectionTypeName,
                             ~nodeTypeName,
                             ~edgesPropNullable,
                             ~edgesNullable,
                             ~nodeNullable,
                           ),
                         )
                       | (
                           None,
                           Some(union),
                           Some({recordName: Some(connectionTypeName)}),
                         ) =>
                         addToStr(
                           printFullGetConnectionNodesFnDefinition(
                             ~connectionLocation,
                             ~connectionPropNullable,
                             ~connectionTypeName,
                             ~nodeTypeName=Printer.printLocalUnionName(union),
                             ~edgesPropNullable,
                             ~edgesNullable,
                             ~nodeNullable,
                           ),
                         )
                       | _ => ()
                       };

                     | _ => ()
                     }
                   );
              | _ => ()
              }
            );
       | _ => ()
       }
     );

  str^;
};

type instruction =
  | ConvertNullableProp
  | ConvertNullableArrayContents
  | ConvertEnum(string)
  | ConvertUnion(string)
  | RootObject(string)
  | HasFragments;

type instructionContainer = {
  atPath: list(string),
  instruction,
};

type converterInstructions = Js.Dict.t(Js.Dict.t(string));

type allConverterInstructions = Js.Dict.t(converterInstructions);

type objectAssets = {
  converterInstructions: allConverterInstructions,
  convertersDefinition: string,
};

let printConvertersMap = (map: Js.Dict.t(string)): string =>
  if (map->Js.Dict.keys->Belt.Array.length == 0) {
    "()";
  } else {
    let str = ref("{");
    let addToStr = s => str := str^ ++ s;

    map->Js.Dict.entries
    |> Tablecloth.Array.forEach(~f=((key, value)) => {
         addToStr("\"" ++ key ++ "\": " ++ value ++ ",")
       });

    addToStr("};");
    str^;
  };

type conversionDirection =
  | Wrap
  | Unwrap;

let objectToAssets =
    (~rootObjects: list(finalizedObj), ~direction=Unwrap, obj: object_)
    : objectAssets => {
  let instructions: array(instructionContainer) = [||];
  let addInstruction = i => instructions |> Js.Array.push(i) |> ignore;

  let subObjectInstructions: Js.Dict.t(converterInstructions) =
    Js.Dict.empty();

  let addSubObjInstruction = (name, instructions) =>
    subObjectInstructions->Js.Dict.set(name, instructions);

  let converters = Js.Dict.empty();

  let rec traversePropType =
          (
            ~converters,
            ~inRootObject,
            ~addInstruction,
            ~propName,
            ~currentPath,
            propType: propType,
          ) =>
    switch (propType) {
    | Scalar(_)
    | FragmentRefValue(_) => ()
    | TypeReference(name) =>
      switch (
        rootObjects->Tablecloth.List.find(
          ~f=
            fun
            | {recordName} when recordName == Some(name) => true
            | _ => false,
        ),
        subObjectInstructions->Js.Dict.get(name),
      ) {
      | (Some(obj), None) =>
        addInstruction({atPath: currentPath, instruction: RootObject(name)});

        if (inRootObject != Some(name)) {
          obj.definition
          |> makeRootObjectInstruction(~name, ~converters)
          |> addSubObjInstruction(name);
        };
      | (Some(_), Some(_)) =>
        addInstruction({atPath: currentPath, instruction: RootObject(name)})
      | (None, Some(_) | None) => ()
      }
    | Enum({name}) =>
      converters->Js.Dict.set(
        Printer.printEnumName(name),
        switch (direction) {
        | Wrap => Printer.printEnumWrapFnReference(name)
        | Unwrap => Printer.printEnumUnwrapFnReference(name)
        },
      );

      addInstruction({atPath: currentPath, instruction: ConvertEnum(name)});
    | Union({atPath, members}) =>
      converters->Js.Dict.set(
        Printer.makeUnionName(atPath),
        switch (direction) {
        | Wrap =>
          Printer.printUnionWrapFnReference(Printer.makeUnionName(atPath))
        | Unwrap =>
          Printer.printUnionUnwrapFnReference(Printer.makeUnionName(atPath))
        },
      );

      addInstruction({
        atPath: currentPath,
        instruction: ConvertUnion(Printer.makeUnionName(atPath)),
      });

      members->Belt.List.forEach(member => {
        member.shape.values
        |> traverseProps(
             ~converters,
             ~inRootObject,
             ~addInstruction,
             ~currentPath=[
               member.name |> Tablecloth.String.toLower,
               ...currentPath,
             ],
           )
      });
    | Array({nullable, propType}) =>
      if (nullable) {
        addInstruction({
          atPath: currentPath,
          instruction: ConvertNullableArrayContents,
        });
      };

      propType
      |> traversePropType(
           ~inRootObject,
           ~addInstruction,
           ~propName,
           ~currentPath,
           ~converters,
         );
    | Object({values}) =>
      values
      |> traverseProps(
           ~converters,
           ~inRootObject,
           ~addInstruction,
           ~currentPath,
         )
    }
  and traverseProps =
      (
        ~currentPath,
        ~addInstruction,
        ~converters,
        ~inRootObject: option(string),
        propValues,
      ) => {
    let hasFragments = ref(false);

    propValues
    |> Tablecloth.Array.forEach(~f=p =>
         switch (p) {
         | FragmentRef(_) => hasFragments := true
         | Prop(name, {nullable, propType}) =>
           let newPath = [name, ...currentPath];

           if (nullable) {
             addInstruction({
               atPath: newPath,
               instruction: ConvertNullableProp,
             });
           };

           propType
           |> traversePropType(
                ~converters,
                ~inRootObject,
                ~addInstruction,
                ~currentPath=newPath,
                ~propName=name,
              );
         }
       );

    if (hasFragments^) {
      addInstruction({atPath: currentPath, instruction: HasFragments});
    };
  }
  and makeRootObjectInstruction = (~name, ~converters, obj: object_) => {
    let instructions: array(instructionContainer) = [||];
    let addInstruction = i => instructions |> Js.Array.push(i) |> ignore;

    let () =
      obj.values
      ->traverseProps(
          ~converters,
          ~inRootObject=Some(name),
          ~addInstruction,
          ~currentPath=[],
        );

    instructions->makeConverterInstructions;
  }
  and makeConverterInstructions = instructions => {
    let dict: converterInstructions = Js.Dict.empty();

    instructions
    |> Tablecloth.Array.forEach(~f=instruction => {
         let key =
           instruction.atPath
           |> Tablecloth.List.reverse
           |> Tablecloth.String.join(~sep="_");

         let action =
           switch (instruction.instruction) {
           | ConvertNullableProp => ("n", "")
           | ConvertNullableArrayContents => ("na", "")
           | ConvertEnum(name) => ("e", Printer.printEnumName(name))
           | ConvertUnion(name) => ("u", name)
           | RootObject(name) => ("r", name)
           | HasFragments => ("f", "")
           };

         switch (dict->Js.Dict.get(key)) {
         | Some(d) =>
           let (actionName, value) = action;
           d->Js.Dict.set(actionName, value);
         | None => dict->Js.Dict.set(key, Js.Dict.fromList([action]))
         };
       });

    dict;
  };

  obj.values
  |> traverseProps(
       ~converters,
       ~inRootObject=None,
       ~addInstruction,
       ~currentPath=[],
     );

  let rootInstructions = makeConverterInstructions(instructions);

  {
    converterInstructions:
      switch (rootInstructions->Js.Dict.keys->Belt.Array.length) {
      | 0 => Js.Dict.empty()
      | _ =>
        Js.Dict.fromList([
          ("__root", rootInstructions),
          ...subObjectInstructions->Js.Dict.entries->Belt.List.fromArray,
        ])
      },
    convertersDefinition: printConvertersMap(converters),
  };
};