"%JAVA_HOME%"\bin\javac.exe -d classes src\*.java ..\..\Java\src\com\epicgames\unreal\CRToken.java
cd classes
"%JAVA_HOME%"\bin\jar cfe ../bin/ConfigRulesTool.jar ConfigRulesTool *.class com/epicgames/unreal/CRToken.class

ECHO "You MUST re-compile this from a macOS environment since iOS also uses this tool."
ECHO "We need that since the macOS version of this script will create an app bundle with "
ECHO "a standalone Java Runtime, thus not unnecessarily requiring the installation of Java."