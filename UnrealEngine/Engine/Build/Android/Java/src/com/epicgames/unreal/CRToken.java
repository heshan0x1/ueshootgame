// Copyright Epic Games, Inc. All Rights Reserved.

package com.epicgames.unreal;

import java.util.HashMap;
import java.util.Map;

public class CRToken {
	public static final int COMMAND_UNKNOWN = -1;
	public static final int COMMAND_SET = 0;
	public static final int COMMAND_CLEAR = 1;
	public static final int COMMAND_CHIPSET = 2;
	public static final int COMMAND_CONDITION = 3;
	public static final int COMMAND_IF = 4;
	public static final int COMMAND_ELSEIF = 5;
	public static final int COMMAND_ELSE = 6;
	public static final int COMMAND_ENDIF = 7;
	public static final int COMMAND_GOTO = 8;
	public static final int COMMAND_EOF = 99;

	public static final int CONDKEY_UNKNOWN = -1;
	public static final int CONDKEY_SOURCETYPE = 0;
	public static final int CONDKEY_COMPARETYPE = 1;
	public static final int CONDKEY_MATCHSTRING = 2;

	public static final int SOURCE_LITERAL = 0;
	public static final int SOURCE_EXIST = 1;
	public static final int SOURCE_PREVREGEX = 2;
	public static final int SOURCE_COMMANDLINE = 3;

	public static final int COMPARE_IGNORE = 50;

	public static final int COMPARE_UNKNOWN = -1;
	public static final int COMPARE_EXIST = 0;
	public static final int COMPARE_NOTEXIST = 1;
	public static final int COMPARE_EQUAL = 2;
	public static final int COMPARE_NOTEQUAL = 3;
	public static final int COMPARE_REGEX = 4;
	public static final int COMPARE_STARTS = 5;
	public static final int COMPARE_CONTAINS = 6;
	public static final int COMPARE_ENDS = 7;
	public static final int COMPARE_VEQUAL = 8;
	public static final int COMPARE_VNOTEQUAL = 9;
	public static final int COMPARE_LESS = 10;
	public static final int COMPARE_LESSEQUAL = 11;
	public static final int COMPARE_GREATER = 12;
	public static final int COMPARE_GREATEREQUAL = 13;

	public static final int SETVARFLAGS_NONE = 0;
	public static final int SETVARFLAGS_APPEND = 1;
	public static final int SETVARFLAGS_EXPAND = 2;

	private static HashMap<String,Integer> CommandTokens = new HashMap<>();
	private static HashMap<String,Integer> CondKeyTokens = new HashMap<>();
	private static HashMap<String,Integer> SourceTokens = new HashMap<>();
	private static HashMap<String,Integer> CompareTokens = new HashMap<>();

	private static HashMap<Integer,String> CommandStrings = new HashMap<>();
	private static HashMap<Integer,String> CondKeyStrings = new HashMap<>();
	private static HashMap<Integer,String> SourceStrings = new HashMap<>();
	private static HashMap<Integer,String> CompareStrings = new HashMap<>();

	public static void Init()
	{
		CommandTokens.put("set", COMMAND_SET);
		CommandTokens.put("clear", COMMAND_CLEAR);
		CommandTokens.put("chipset", COMMAND_CHIPSET);
		CommandTokens.put("condition", COMMAND_CONDITION);
		CommandTokens.put("if", COMMAND_IF);
		CommandTokens.put("elseif", COMMAND_ELSEIF);
		CommandTokens.put("else", COMMAND_ELSE);
		CommandTokens.put("endif", COMMAND_ENDIF);

		CondKeyTokens.put("SourceType", CONDKEY_SOURCETYPE);
		CondKeyTokens.put("CompareType", CONDKEY_COMPARETYPE);
		CondKeyTokens.put("MatchString", CONDKEY_MATCHSTRING);

		SourceTokens.put("[EXIST]", SOURCE_EXIST);
		SourceTokens.put("SRC_PreviousRegexMatch", SOURCE_PREVREGEX);
		SourceTokens.put("SRC_CommandLine", SOURCE_COMMANDLINE);

		CompareTokens.put("CMP_Exist", COMPARE_EXIST);
		CompareTokens.put("CMP_NotExist", COMPARE_NOTEXIST);
		CompareTokens.put("CMP_Equal", COMPARE_EQUAL);
		CompareTokens.put("CMP_NotEqual", COMPARE_NOTEQUAL);
		CompareTokens.put("CMP_EqualIgnore", COMPARE_EQUAL + COMPARE_IGNORE);
		CompareTokens.put("CMP_NotEqualIgnore", COMPARE_NOTEQUAL + COMPARE_IGNORE);
		CompareTokens.put("CMP_Regex", COMPARE_REGEX);
		CompareTokens.put("CMP_Starts", COMPARE_STARTS);
		CompareTokens.put("CMP_Contains", COMPARE_CONTAINS);
		CompareTokens.put("CMP_Ends", COMPARE_ENDS);
		CompareTokens.put("CMP_VEqual", COMPARE_VEQUAL);
		CompareTokens.put("CMP_VNotEqual", COMPARE_VNOTEQUAL);
		CompareTokens.put("CMP_Less", COMPARE_LESS);
		CompareTokens.put("CMP_LessIgnore", COMPARE_LESS + COMPARE_IGNORE);
		CompareTokens.put("CMP_LessEqual", COMPARE_LESSEQUAL);
		CompareTokens.put("CMP_LessEqualIgnore", COMPARE_LESSEQUAL + COMPARE_IGNORE);
		CompareTokens.put("CMP_Greater", COMPARE_GREATER);
		CompareTokens.put("CMP_GreaterIgnore", COMPARE_GREATER + COMPARE_IGNORE);
		CompareTokens.put("CMP_GreaterEqual", COMPARE_GREATEREQUAL);
		CompareTokens.put("CMP_GreaterEqualIgnore", COMPARE_GREATEREQUAL + COMPARE_IGNORE);
	}

	public static void InitStrings()
	{
		for (Map.Entry<String, Integer> entry : CommandTokens.entrySet())
		{
			CommandStrings.put((Integer)entry.getValue(), (String)entry.getKey());
		}
		for (Map.Entry<String, Integer> entry : CondKeyTokens.entrySet())
		{
			CondKeyStrings.put((Integer)entry.getValue(), (String)entry.getKey());
		}
		for (Map.Entry<String, Integer> entry : SourceTokens.entrySet())
		{
			SourceStrings.put((Integer)entry.getValue(), (String)entry.getKey());
		}
		for (Map.Entry<String, Integer> entry : CompareTokens.entrySet())
		{
			CompareStrings.put((Integer)entry.getValue(), (String)entry.getKey());
		}
	}

	public static int findCommand(String in)
	{
		return CommandTokens.getOrDefault(in, COMMAND_UNKNOWN);
	}

	public static int findCondKey(String in)
	{
		return CondKeyTokens.getOrDefault(in, CONDKEY_UNKNOWN);
	}

	public static int findSource(String in)
	{
		return SourceTokens.getOrDefault(in, SOURCE_LITERAL);
	}

	public static int findCompare(String in)
	{
		return CompareTokens.getOrDefault(in, COMPARE_UNKNOWN);
	}

	public static String getCommand(int token)
	{
		return CommandStrings.getOrDefault(token, "UNKNOWN");
	}

	public static String getCondKey(int token)
	{
		return CondKeyStrings.getOrDefault(token, "UNKNOWN");
	}

	public static String getSource(int token)
	{
		return SourceStrings.getOrDefault(token, "UNKNOWN");
	}

	public static String getCompare(int token)
	{
		return CompareStrings.getOrDefault(token, "UNKNOWN");
	}
}

