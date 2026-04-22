#!/bin/bash

# compiles the .java into .class
javac -d classes src/*.java ../../Java/src/com/epicgames/unreal/CRToken.java

pushd classes
# makes the jar from the class
jar cfe ../bin/ConfigRulesTool.jar ConfigRulesTool *.class com/epicgames/unreal/CRToken.class
popd

# creates the .app standalone with the previously generated jar
jpackage --input ./bin/ --name ConfigRulesTool --main-jar ConfigRulesTool.jar --main-class ConfigRulesTool

# mounts the dmg
hdiutil attach ./ConfigRulesTool-1.0.dmg

# copies the .app tho the IOS world
cp -R /Volumes/ConfigRulesTool/ConfigRulesTool.app ../../../IOS/ConfigRulesTool/

# unmounts the dmg
hdiutil detach /Volumes/ConfigRulesTool

# removes the dmg
rm ./ConfigRulesTool-1.0.dmg
