// Copyright Epic Games, Inc. All Rights Reserved.

import java.io.*;
import java.util.*;
import java.util.zip.*;

import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;

import javax.crypto.Cipher;
import javax.crypto.SecretKey;
import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.PBEKeySpec;
import javax.crypto.spec.SecretKeySpec;

import com.epicgames.unreal.CRToken;

	class ByteArrayInputStream2 extends InputStream
	{
		private byte[] data;
		private int pos;
		private int mark;

		public ByteArrayInputStream2(byte[] data)
		{
			this.data = data;
			this.pos = 0;
			this.mark = 0;
		}

		@Override
		public int read() throws IOException
		{
			if (pos < data.length)
			{
	            return data[pos++] & 0xff;
			}
			return -1;
		}

		public int peek() throws IOException
		{
			if (pos < data.length)
			{
				return data[pos] & 0xff;
			}
			return -1;
		}

		public int read(byte[] buffer, int offset, int length)
		{
			int count = Math.min(data.length, pos + length) - pos;
			count = (offset + count > buffer.length) ? buffer.length - offset : count;

			System.arraycopy(data, pos, buffer, offset, count);
			pos += count;
			return count;
		}

		public int readShort() throws IOException
		{
			return (read() << 8) | read();
		}

		public int readInt() throws IOException
		{
			return (read() << 24) | (read() << 16) | (read() << 8) | read();
		}

		@Override
		public int available() throws IOException
		{
			return data.length - pos;
	    }

		public int seek(int position)
		{
			if (position >= 0 && position < data.length)
			{
				pos = position;
			}
			return pos;
		}

		public int getPosition()
		{
			return pos;
		}

		@Override
		public void close() throws IOException
		{
			data = null;
			pos = -1;
		}

		@Override
		public boolean markSupported()
		{
			return true;
		}

		@Override
		public synchronized void mark(int readlimit)
		{
			mark = pos;
		}

		@Override
		public synchronized void reset() throws IOException
		{
			pos = mark;
		}

		@Override
		public long skip(long n) throws IOException
		{
			long skipped = Math.min(n, available());
			pos += skipped;
			return skipped;
	    }
	}


public class ConfigRulesTool
{
	public static String CipherTransform = "AES/ECB/PKCS5Padding";
	
	public static void writeInt(FileOutputStream outStream, int value)
	{
		try
		{
			outStream.write((value >> 24) & 0xff);
			outStream.write((value >> 16) & 0xff);
			outStream.write((value >> 8) & 0xff);
			outStream.write(value & 0xff);
		}
		catch (Exception e)
		{
		}
	}
	
	public static SecretKey generateKey(String password)
	{
		byte[] salt = new byte[] { 0x23, 0x71, (byte)0xd3, (byte)0xa3, 0x30, 0x71, 0x63, (byte)0xe3 };
		try
		{
			SecretKey secret = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA1").generateSecret(new PBEKeySpec(password.toCharArray(), salt, 1000, 128));
			return new SecretKeySpec(secret.getEncoded(), "AES");
		}
		catch (Exception e)
		{
		}
		return new SecretKeySpec(salt, "AES");
	}
	
	public static void main(String[] args)
	{
		String inFilename = "configrules.txt";
		String outFilename = "configrules.bin.png";
		String command = "";
		String key = "";
		int headerSize = 2 + 4 + 4;
		boolean bVerbose = false;
		
		System.out.println("ConfigRulesTool v1.2\n");

		if (args.length == 0)
		{
			System.out.println("Usage: [op] [-verbose] inputfile outputfile [key]\n");
			System.out.println("\tc = compress and encrypt if key provided");
			System.out.println("\td = decrypt and decompress");
			System.out.println("\tp = parse, compress and encrypt if key provided");
			System.exit(0);
		}
		
		command = args[0];
		int nextParam = 1;
		for (int argIndex=1; argIndex < args.length; argIndex++)
		{
			if (args[argIndex].equals("-verbose"))
			{
				bVerbose = true;
				continue;
			}
			switch (nextParam)
			{
				case 1:
					inFilename = args[argIndex];
					nextParam++;
					break;
					
				case 2:
					outFilename = args[argIndex];
					nextParam++;
					break;

				case 3:
					key = args[argIndex];
					nextParam++;
					break;
			}
		}	
		
		if (command.equals("c") || command.equals("p"))
		{
			byte[] bytesToCompress = null;
			Path path = Paths.get(inFilename);
			try
			{
				bytesToCompress = Files.readAllBytes(path);
			}
			catch (IOException e)
			{
				System.out.println("Unable to read file: " + inFilename);
				System.exit(-1);
			}
			int sizeUncompressed = bytesToCompress.length;

			int version = -1;
			try
			{
				int versionBytes = sizeUncompressed < 80 ? sizeUncompressed : 80;
				String versionLine = new String(bytesToCompress, 0, versionBytes, "UTF-8");
				if (versionLine.startsWith("// version:"))
				{
					int eolIndex = versionLine.indexOf("\r");
					int newIndex = versionLine.indexOf("\n");
					eolIndex = (eolIndex < 0) ? 1000 : eolIndex;
					newIndex = (newIndex < 0) ? 1000 : newIndex;
					eolIndex = (eolIndex < newIndex) ? eolIndex : newIndex;
					try
					{
						version = Integer.parseInt(versionLine.substring(11, eolIndex));
					}
					catch (Exception e)
					{
						System.out.println("Unable to read version: " + inFilename);
						System.exit(-1);
					}
				}
			}
			catch (UnsupportedEncodingException e)
			{
				System.out.println("Unable to read version: " + inFilename);
				System.exit(-1);
			}
			if (version == -1)
			{
				System.out.println("Unable to read version: " + inFilename);
				System.exit(-1);
			}
			
			// handle parsing request instead of using raw text
			if (command.equals("p"))
			{
				bytesToCompress = parseConfigRules(bytesToCompress, bVerbose);
				if (bytesToCompress == null)
				{
					System.exit(-1);
				}
				sizeUncompressed = bytesToCompress.length;
			}
			
			Deflater deflater = new Deflater();
			deflater.setInput(bytesToCompress);
			deflater.finish();

			byte[] bytesCompressed = new byte[sizeUncompressed * 3];
			int sizeCompressed = deflater.deflate(bytesCompressed);

			// encrypt if key provided
			if (!key.equals(""))
			{
				try
				{
					Cipher cipher = Cipher.getInstance(CipherTransform);
					cipher.init(Cipher.ENCRYPT_MODE, generateKey(key));
					byte[] encrypted = cipher.doFinal(bytesCompressed, 0, sizeCompressed);
					bytesCompressed = encrypted;
					sizeCompressed = bytesCompressed.length;
				}
				catch (Exception e)
				{
					System.out.println("Unable to encrypt input file: " + inFilename);
					System.out.println(e.toString());
					System.exit(-1);
				}
			}

			File outFile = new File(outFilename);
			FileOutputStream fileOutStream = null;
			try
			{
				fileOutStream = new FileOutputStream(outFile);
				byte[] signature = new byte[2];
				signature[0] = (byte)0x39;
				signature[1] = command.equals("p") ? (byte)0xda : (byte)0xd8;
				fileOutStream.write(signature);
				writeInt(fileOutStream, version);
				writeInt(fileOutStream, sizeUncompressed);
				fileOutStream.write(bytesCompressed, 0, sizeCompressed);
			}
			catch (IOException e)
			{
				System.out.println("Error writing file: " + outFilename);
				System.exit(-1);
			}
			try
			{
				if (fileOutStream != null)
				{
					fileOutStream.close();
				}
			}
			catch (IOException e)
			{
				System.out.println("Error writing file: " + outFilename);
				System.exit(-1);
			}

			System.out.println("Version: " + Integer.toString(version) + ", Compressed from " + Integer.toString(sizeUncompressed) +
				" bytes to " + Integer.toString(sizeCompressed + headerSize) + " bytes" + (key.equals("") ? "." : " and encrypted."));

			CRC32 crc = new CRC32();
			path = Paths.get(outFilename);
			byte[] bytesToCRC = null;
			try
			{
				bytesToCRC = Files.readAllBytes(path);
			}
			catch (IOException e)
			{
				System.out.println("Unable to read file: " + outFilename);
				System.exit(-1);
			}
			crc.update(bytesToCRC);
			System.out.println(String.format("CRC32: %08X", crc.getValue()));

			System.exit(0);
		}
	
		if (command.equals("d"))
		{
			byte[] bytesToDecompress = null;
			Path path = Paths.get(inFilename);
			try
			{
				bytesToDecompress = Files.readAllBytes(path);
			}
			catch (IOException e)
			{
				System.out.println("Unable to read file: " + inFilename);
				System.exit(-1);
			}
			int sizeCompressed = bytesToDecompress.length - headerSize;
			if (bytesToDecompress.length < headerSize)
			{
				System.out.println("Input file is invalid: " + inFilename);
				System.exit(-1);
			}
			
			ByteBuffer buffer = ByteBuffer.wrap(bytesToDecompress);
			int signature = buffer.getShort();
			boolean bParsed = (signature == 0x39da);
			if (!bParsed && signature != 0x39d8)
			{
				System.out.println("Input file signature is invalid: " + inFilename + ", " + Integer.toString(signature));
				System.exit(-1);
			}

			int version = buffer.getInt();
			int sizeUncompressed = buffer.getInt();

			// decrypt if key provided
			if (!key.equals(""))
			{
				try
				{
					Cipher cipher = Cipher.getInstance(CipherTransform);
					cipher.init(Cipher.DECRYPT_MODE, generateKey(key));
					byte[] decrypted = cipher.doFinal(bytesToDecompress, headerSize, sizeCompressed);
					sizeCompressed = decrypted.length;
					System.arraycopy(decrypted, 0, bytesToDecompress, headerSize, sizeCompressed);
				}
				catch (Exception e)
				{
					System.out.println("Unable to decrypt input file: " + inFilename);
					System.out.println(e.toString());
					System.exit(-1);
				}
			}
			
			byte[] bytesDecompressed = new byte[sizeUncompressed];
			try
			{
				Inflater inflater = new Inflater();
				inflater.setInput(bytesToDecompress, headerSize, sizeCompressed);
				int resultLength = inflater.inflate(bytesDecompressed);
				inflater.end();
				if (resultLength != sizeUncompressed)
				{
					System.out.println("Error decompressing (size mismatch) file: " + inFilename);
					System.exit(-1);
				}
			}
			catch (Exception e)
			{
				System.out.println("Error decompressing file: " + inFilename);
				System.exit(-1);
			}
			System.out.println("Version: " + Integer.toString(version) + ", Uncompressed size: " + Integer.toString(sizeUncompressed));
			
			if (bParsed)
			{
				if ((bytesDecompressed = decompileConfigRules(bytesDecompressed, bVerbose)) == null)
				{
					System.out.println("Error decompiling file: " + inFilename);
					System.exit(-1);
				}
			}

			File outFile = new File(outFilename);
			FileOutputStream fileOutStream = null;
			try
			{
				fileOutStream = new FileOutputStream(outFile);
				fileOutStream.write(bytesDecompressed);
			}
			catch (IOException e)
			{
				System.out.println("Error writing file: " + outFilename);
				System.exit(-1);
			}
			try
			{
				if (fileOutStream != null)
				{
					fileOutStream.close();
				}
			}
			catch (IOException e)
			{
				System.out.println("Error writing file: " + outFilename);
				System.exit(-1);
			}

			System.out.println("Wrote file: " + outFilename);
			System.exit(0);
		}

		System.out.println("Unknown op: " + command);
		System.exit(-1);
	}
	
	// ===== configrules preparser =====

	static int RegExFound = 0;
	static int RegExRemoved = 0;

	class StringTable
	{
		static public HashMap<String, Integer> stringTable = new HashMap<>();
		static public ByteArrayOutputStream stringOffsets = new ByteArrayOutputStream();
		static public ByteArrayOutputStream stringLengths = new ByteArrayOutputStream();
		static public ByteArrayOutputStream stringData = new ByteArrayOutputStream();

		static private int count = 0;
		static private int stringLen = 0;
		static private int[] offsets = null;
		static private int[] lengths = null;
		static private byte[] data = null;
		
		static public int calls = 0;

		static int count()
		{
			return stringTable.size();
		}
		
		static void close() throws IOException
		{
			//stringOffsets.close();
			//stringLengths.close();
			//stringData.close();
		}

		static int addString(String input)
		{
			calls++;
			int nextIndex = count();
			int findIndex = stringTable.getOrDefault(input, nextIndex);
			if (findIndex == nextIndex)
			{
				stringTable.put(input, nextIndex);
				
				int offset = stringData.size();
				stringOffsets.write(offset >> 8);
				stringOffsets.write(offset & 255);

				int len = input.length();
				stringLengths.write(len >> 8);
				stringLengths.write(len & 255);

				stringData.write(input.getBytes(), 0, len);
			}
			return findIndex;
		}

		static boolean Init(ByteArrayInputStream2 stream)
		{
			try
			{
				count = stream.readShort();
				stringLen = stream.readShort();

				offsets = new int[count];
				for (int i=0; i<count; i++)
				{
					offsets[i] = stream.readShort();
				}

				lengths = new int[count];
				for (int i=0; i<count; i++)
				{
					lengths[i] = stream.readShort();
				}

				data = new byte[stringLen];
				stream.read(data, 0, stringLen);
			}
			catch (Exception e)
			{
				return false;
			}
			return true;
		}

		static String getString(int index)
		{
			if (index < 0 || index >= count)
			{
				return null;
			}
			return new String(data, offsets[index], lengths[index], StandardCharsets.UTF_8);
		}
	}
	
	// removes the entry and exit from ends of the string if present and paired
	private static String RemoveSurrounds(String input, String entry, String exit)
	{
		int entryLength = entry.length();
		int exitLength = exit.length();
		int inputLength = input.length();

		if (inputLength >=2 && entryLength > 0 && entryLength == exitLength)
		{
			char start = input.charAt(0);
			char end = input.charAt(inputLength-1);

			for (int index=0; index < entryLength; index++)
			{
				if (entry.charAt(index) == start && exit.charAt(index) == end)
				{
					return input.substring(1, inputLength-1);
				}
			}
		}
		return input;
	}

	// returns list of strings separated by split using entry/exit as pairing sets (ex. "(" and ")").  The entry and exit characters need to be in same order
	private static ArrayList<String> ParseSegments(String input, char split, String entry, String exit)
	{
		ArrayList<String> output = new ArrayList<String>();
		Stack<Integer> entryStack = new Stack<Integer>();

		int startIndex = 0;
		int scanIndex = 0;
		int exitIndex = -1;
		int inputLength = input.length();
		while (scanIndex < inputLength)
		{
			char scan = input.charAt(scanIndex);

			if (scan == split && entryStack.empty())
			{
				output.add(input.substring(startIndex, scanIndex).trim());
				scanIndex++;
				startIndex = scanIndex;
				continue;
			}
			scanIndex++;

			if (scan == '\\')
			{
				scanIndex++;
				continue;
			}

			if (!entryStack.empty() && exit.indexOf(scan) == exitIndex)
			{
				entryStack.pop();
				exitIndex = entryStack.empty() ? -1 : entryStack.peek();
				continue;
			}

			int entryIndex = entry.indexOf(scan);
			if (entryIndex >= 0)
			{
				entryStack.add(entryIndex);
				exitIndex = entryIndex;
				continue;
			}
		}
		if (startIndex < inputLength)
		{
			output.add(input.substring(startIndex).trim());
		}

		return output;
	}

	static String TestRegEx(String input)
	{
		// special RegEx symbols that could be escaped
		String regex = ".*+?|$^()[]{}-\\";
		int regexLength = regex.length();
					
		// remove each escaped symbol then check if it is still in string
		String test = input;
		for (int rIndex = 0; rIndex < regexLength; rIndex++)
		{
			String symbol = "" + regex.charAt(rIndex);
			String replace = "\\" + symbol;
			int escapeIndex = test.indexOf(replace);
			while (escapeIndex >= 0)
			{
				test = test.substring(0, escapeIndex) + (escapeIndex - 2 < test.length() ? test.substring(escapeIndex + 2) : "");
				escapeIndex = test.indexOf(replace);
			}
			if (test.indexOf(symbol) >= 0)
			{
				// can't replace it
				return null;
			}
		}
					
		// make a version without any escapes
		for (int rIndex = 0; rIndex < regexLength; rIndex++)
		{
			String symbol = "" + regex.charAt(rIndex);
			String replace = "\\" + symbol;
			int escapeIndex = input.indexOf(replace);
			while (escapeIndex >= 0)
			{
				input = input.substring(0, escapeIndex) + (escapeIndex - 1 < test.length() ? input.substring(escapeIndex + 1) : "");
				escapeIndex = input.indexOf(replace);
			}
		}
					
		return input;
	}
	
	private static boolean parseConditions(ByteArrayOutputStream stream, ArrayList<String> conditions, int lineNumber, boolean bVerbose)
	{
		int condLen = conditions.size();
		stream.write(condLen);

		boolean bFirst = false;//true;
		for (String condition : conditions)
		{
			int sourceToken = -1;
			int compareToken = -1;
			String SourceType = "";
			String CompareType = "";
			String MatchString = "";
			int foundBits = 0;

			// deal with condition group (src,cmp,match)
			ArrayList<String> groups = ParseSegments(RemoveSurrounds(condition, "(", ")"), ',', "\"", "\"");
			for (String group : groups)
			{
				ArrayList<String> keyvalue = ParseSegments(group, '=', "\"", "\"");
				if (keyvalue.size() == 2)
				{
					String key = RemoveSurrounds(keyvalue.get(0), "\"", "\"");
					String value = RemoveSurrounds(keyvalue.get(1), "\"", "\"");

					int keyToken = CRToken.findCondKey(key);
					switch (keyToken)
					{
						case CRToken.CONDKEY_SOURCETYPE:
							sourceToken = CRToken.findSource(value);
							if (sourceToken == -1)
							{
								sourceToken = CRToken.SOURCE_LITERAL;
							}
							SourceType = value;
							foundBits |= 1;
							break;
							
						case CRToken.CONDKEY_COMPARETYPE:
							compareToken = CRToken.findCompare(value);
							CompareType = value;
							if (compareToken == CRToken.COMPARE_UNKNOWN)
							{
								System.out.println("Error: condition comparetype '" + value + "' invalid, line " + lineNumber);
								return false;
							}
							foundBits |= 2;
							break;
							
						case CRToken.CONDKEY_MATCHSTRING:
							MatchString = value;
							foundBits |= 4;
							break;
							
						default:
							System.out.println("Error: condition type '" + key + "' invalid, line " + lineNumber);
							return false;
							
					}
				}
			}
			
			// make sure we got all 3
			if (foundBits != 0x07)
			{
				if ((foundBits & 1) == 0)
				{
					System.out.println("Error: SourceType missing, line " + lineNumber);
				}
				if ((foundBits & 2) == 0)
				{
					System.out.println("Error: CompareType missing, line " + lineNumber);
				}
				if ((foundBits & 4) == 0)
				{
					System.out.println("Error: MatchString missing, line " + lineNumber);
				}
				return false;
			}
			
			// go ahead and make lowercase if ignore compare type
			if (compareToken >= CRToken.COMPARE_IGNORE)
			{
				MatchString = MatchString.toLowerCase();
			}

			// attempt to optimize Regex (it is expensive)
			if (compareToken == CRToken.COMPARE_REGEX)
			{
				RegExFound++;

				if (MatchString.startsWith("^"))
				{
					// possibly COMPARETYPE_STARTS
					String test = TestRegEx(MatchString.substring(1));
					if (test != null)
					{
						compareToken = CRToken.COMPARE_STARTS;
						CompareType = "CMP_Starts";
						MatchString = test;
						RegExRemoved++;
						if (bVerbose)
						{
							System.out.println("--RegEx optimize (" + SourceType + "," + CompareType + ",\"" + MatchString + "\"), line " + lineNumber);
						}
					}
				}
				else if (MatchString.endsWith("$") && !MatchString.endsWith("\\$"))
				{
					// possibly COMPARETYPE_ENDS
					String test = TestRegEx(MatchString.substring(0, MatchString.length() - 1));
					if (test != null)
					{
						compareToken = CRToken.COMPARE_ENDS;
						CompareType = "CMP_Ends";
						MatchString = test;
						RegExRemoved++;
						if (bVerbose)
						{
							System.out.println("--RegEx optimize (" + SourceType + "," + CompareType + ",\"" + MatchString + "\"), line " + lineNumber);
						}
					}
				}
				else
				{
					// possibly COMPARE_TYPE_CONTAINS
					String test = TestRegEx(MatchString);
					if (test != null)
					{
						compareToken = CRToken.COMPARE_CONTAINS;
						CompareType = "CMP_Contains";
						MatchString = test;
						RegExRemoved++;
						if (bVerbose)
						{
							System.out.println("--RegEx optimize (" + SourceType + "," + CompareType + ",\"" + MatchString + "\"), line " + lineNumber);
						}
					}
				}
			}

			if (bFirst)
			{
				bFirst = false;
				System.out.println("--(" + SourceType + "," + CompareType + "," + MatchString + ")");
			}

			// write condition
			stream.write(sourceToken);
			int sourceIndex = StringTable.addString(SourceType);
			stream.write(sourceIndex >> 8);
			stream.write(sourceIndex & 255);
			stream.write(compareToken);
			int matchIndex = StringTable.addString(MatchString);
			stream.write(matchIndex >> 8);
			stream.write(matchIndex & 255);
		}
		
		return true;
	}

	private static byte[] parseConfigRules(byte[] sourceData, boolean bVerbose)
	{
		int configRulesVersion = 0;

		int lineNumber = 0;
		int instructionCount = 0;

		CRToken.Init();
		
		BufferedReader reader = null;
		InputStream stream = new ByteArrayInputStream(sourceData);
		if (stream != null)
		{
			try
			{
				reader = new BufferedReader(new InputStreamReader(stream));
			}
			catch (Exception e)
			{
				System.out.println("Failed to create config rules reader. " + e);
				return null;
			}
		}
		else
		{
			System.out.println("Unable to create ByteArrayInputStream.");
			return null;
		}
		try
		{
			ByteArrayOutputStream memStream = new ByteArrayOutputStream();
			
			// doesn't really need to be a hash but not critical
			HashMap<Integer, Integer> gotoFixups = new HashMap<Integer, Integer>();
			Stack<Integer> endifOffsets = new Stack<Integer>();
			Stack<Stack<Integer>> recoverOffsets = new Stack<Stack<Integer>>();
			Stack<Integer> falseOffsets = new Stack<Integer>();
			int falseOffset = -1;
			int ifDepth = 0;

			String line;
			while ((line = reader.readLine()) != null)
			{
				lineNumber++;
				line = line.trim();
				if (line.length() < 1)
				{
					continue;
				}
				if (line.startsWith("//") || line.startsWith(";"))
				{
					if (line.startsWith("// version:"))
					{
						configRulesVersion = Integer.parseInt(line.substring(11));
						System.out.println("ConfigRules version: " + configRulesVersion);
					}
					continue;
				}

				// look for command
				int index = line.indexOf(':');
				if (index == -1)
				{
					continue;
				}
				String original = line;
				String command = line.substring(0, index).trim();
				line = line.substring(index + 1).trim();

				int commandToken = CRToken.findCommand(command);
				switch (commandToken)
				{
					case CRToken.COMMAND_SET:
						// set:(a=b[,c=d,...])
						ArrayList<String> sets = ParseSegments(RemoveSurrounds(line, "(", ")"), ',', "(\"", ")\"");
						int setsLen = sets.size();
						if (setsLen == 0)
						{
							System.out.println("Error: set has no sets, line " + lineNumber);
							return null;
						}
						else if (setsLen > 255)
						{
							System.out.println("Error: set has too many sets, line " + lineNumber);
							return null;
						}

						try
						{
							ByteArrayOutputStream setStream = new ByteArrayOutputStream();
							setStream.write(setsLen);
							for (String assignment : sets)
							{
								ArrayList<String> keyvalue = ParseSegments(assignment, '=', "\"", "\"");
								if (keyvalue.size() == 2)
								{
									String key = RemoveSurrounds(keyvalue.get(0), "\"", "\"");
									String value = RemoveSurrounds(keyvalue.get(1), "\"", "\"");
									
									int flags = CRToken.SETVARFLAGS_NONE;
									if (key.startsWith("APPEND_"))
									{
										flags |= CRToken.SETVARFLAGS_APPEND;
										key = key.substring(7);
									}
									if (value.contains("$("))
									{
										flags |= CRToken.SETVARFLAGS_EXPAND;
									}

									setStream.write(flags);

									int keyIndex = StringTable.addString(key);
									setStream.write(keyIndex >> 8);
									setStream.write(keyIndex & 255);

									int valueIndex = StringTable.addString(value);
									setStream.write(valueIndex >> 8);
									setStream.write(valueIndex & 255);
								}
								else
								{
									System.out.println("Error: set has mismatched key/value pairs, line " + lineNumber);
									return null;
								}
							}
							//setStream.close();
							byte[] setData = setStream.toByteArray();
							int setDataLen = setData.length;

							instructionCount++;
							memStream.write(commandToken);
							memStream.write(setDataLen >> 8);
							memStream.write(setDataLen & 255);
							memStream.write(setData, 0, setDataLen);
						}
						catch (Exception e)
						{
							System.out.println("Error: set exception, line " + lineNumber + ": " + e);
						}
						break;

					case CRToken.COMMAND_CLEAR:
						// clear:(a[,b,...])
						ArrayList<String> clears = ParseSegments(RemoveSurrounds(line, "(", ")"), ',', "(\"", ")\"");
						int clearsLen = clears.size();
						if (clearsLen == 0)
						{
							System.out.println("Error: clear has no sets, line " + lineNumber);
							return null;
						}
						else if (clearsLen > 255)
						{
							System.out.println("Error: clear has too many variables, line " + lineNumber);
							return null;
						}

						try
						{
							ByteArrayOutputStream clearStream = new ByteArrayOutputStream();
							clearStream.write(clearsLen);

							for (String entry : clears)
							{	
								String key = RemoveSurrounds(entry, "\"", "\"");

								int keyIndex = StringTable.addString(key);
								clearStream.write(keyIndex >> 8);
								clearStream.write(keyIndex & 255);
							}
							//clearStream.close();
							byte[] clearData = clearStream.toByteArray();
							int clearDataLen = clearData.length;

							instructionCount++;
							memStream.write(commandToken);
							memStream.write(clearDataLen >> 8);
							memStream.write(clearDataLen & 255);
							memStream.write(clearData, 0, clearDataLen);
						}
						catch (Exception e)
						{
							System.out.println("Error: clear exception, line " + lineNumber + ": " + e);
						}
						break;

					case CRToken.COMMAND_CHIPSET:
						// chipset:"hardware",useAffinity,"chipset","cpu",processorCount,bigCoreMask,littleCoreMask
						ArrayList<String> values = ParseSegments(line, ',', "\"", "\"");
						if (values.size() == 7)
						{
							try
							{
								ByteArrayOutputStream chipStream = new ByteArrayOutputStream();

								String originalKey = RemoveSurrounds(values.get(0), "\"", "\"");
								String key = originalKey.toLowerCase();
								int keyIndex = StringTable.addString(key);
								chipStream.write(keyIndex >> 8);
								chipStream.write(keyIndex & 255);

								for (int i=1; i<7; i++)
								{
									String value = RemoveSurrounds(values.get(i), "\"", "\"");
									int valueIndex = StringTable.addString(value);
									chipStream.write(valueIndex >> 8);
									chipStream.write(valueIndex & 255);
								}
								
								// add a string with delta offsets for uppercase characters for decompile (not needed for runtime)
								{
									String deltas = "";
									int lastScan = 0;
									for (int scan=0; scan < originalKey.length(); scan++)
									{
										if (originalKey.charAt(scan) != key.charAt(scan))
										{
											int delta = scan - lastScan;
											lastScan = scan;
											deltas += Character.toString(delta);
										}
									}
									int deltaIndex = StringTable.addString(deltas);
									chipStream.write(deltaIndex >> 8);
									chipStream.write(deltaIndex & 255);
								}
								
								//chipStream.close();
								byte[] chipData = chipStream.toByteArray();
								int chipDataLen = chipData.length;

								instructionCount++;
								memStream.write(commandToken);
								memStream.write(chipDataLen >> 8);
								memStream.write(chipDataLen & 255);
								memStream.write(chipData, 0, chipDataLen);
							}
							catch (Exception e)
							{
								System.out.println("Error: chipset exception, line " + lineNumber + ": " + e);
							}
						}
						else
						{
							System.out.println("Error: chipset has missing values, line " + lineNumber);
							return null;
						}
						break;

					case CRToken.COMMAND_CONDITION:
						// condition:((SourceType=SRC_DeviceMake,CompareType=CMP_Equal,MatchString="samsung")),(SourceType=,CompareType=,MatchString=),...]),(a=b[,c=d,...]),(a[,b,...])
						// if all the conditions are true, execute the optional sets and/or clears
						ArrayList<String> conditionAndSets = ParseSegments(line, ',', "(\"", ")\"");
						int setsize = conditionAndSets.size();
						if (setsize == 2 || setsize == 3)
						{
							ArrayList<String> conditions = ParseSegments(RemoveSurrounds(conditionAndSets.get(0), "(", ")"), ',', "(\"", ")\"");
							ArrayList<String> csets = ParseSegments(RemoveSurrounds(conditionAndSets.get(1), "(", ")"), ',', "(\"", ")\"");
							ArrayList<String> cclears = (setsize == 3) ? ParseSegments(RemoveSurrounds(conditionAndSets.get(2), "(", ")"), ',', "(\"", ")\"") : new ArrayList<String>();

							int condLen = conditions.size();
							if (condLen == 0)
							{
								System.out.println("Error: condition has no conditions, line " + lineNumber);
								return null;
							}
							else if (condLen > 255)
							{
								System.out.println("Error: condition has too many conditions, line " + lineNumber);
							}

							try
							{
								ByteArrayOutputStream condStream = new ByteArrayOutputStream();

								// deal with conditions parsing
								if (!parseConditions(condStream, conditions, lineNumber, bVerbose))
								{
									return null;
								}

								// sets
								int csetsLen = csets.size();
								if (csetsLen > 255)
								{
									System.out.println("Error: condition has too many sets, line " + lineNumber);
									return null;
								}

								try
								{
									ByteArrayOutputStream setStream = new ByteArrayOutputStream();
									setStream.write(csetsLen);
									for (String assignment : csets)
									{
										ArrayList<String> keyvalue = ParseSegments(assignment, '=', "\"", "\"");
										if (keyvalue.size() == 2)
										{
											String key = RemoveSurrounds(keyvalue.get(0), "\"", "\"");
											String value = RemoveSurrounds(keyvalue.get(1), "\"", "\"");

											int flags = CRToken.SETVARFLAGS_NONE;
											if (key.startsWith("APPEND_"))
											{
												flags |= CRToken.SETVARFLAGS_APPEND;
												key = key.substring(7);
											}
											if (value.contains("$("))
											{
												flags |= CRToken.SETVARFLAGS_EXPAND;
											}

											setStream.write(flags);

											int keyIndex = StringTable.addString(key);
											setStream.write(keyIndex >> 8);
											setStream.write(keyIndex & 255);

											int valueIndex = StringTable.addString(value);
											setStream.write(valueIndex >> 8);
											setStream.write(valueIndex & 255);
										}
										else
										{
											System.out.println("Error: condition set has mismatched key/value pairs line " + lineNumber);
											return null;
										}
									}
									//setStream.close();
									byte[] setData = setStream.toByteArray();
									int setDataLen = setData.length;
									condStream.write(setData, 0, setDataLen);
								}
								catch (Exception e)
								{
									System.out.println("Error: condition exception, line " + lineNumber + ": " + e);
								}

								// clears
								int cclearsLen = cclears.size();
								if (cclearsLen > 255)
								{
									System.out.println("Error: condition has too many clears, line " + lineNumber);
									return null;
								}
							
								try
								{
									ByteArrayOutputStream clearStream = new ByteArrayOutputStream();
									clearStream.write(cclearsLen);

									for (String entry : cclears)
									{	
										String key = RemoveSurrounds(entry, "\"", "\"");

										int keyIndex = StringTable.addString(key);
										clearStream.write(keyIndex >> 8);
										clearStream.write(keyIndex & 255);
									}
									//clearStream.close();
									byte[] clearData = clearStream.toByteArray();
									int clearDataLen = clearData.length;
									condStream.write(clearData, 0, clearDataLen);
								}
								catch (Exception e)
								{
									System.out.println("Error: clear exception, line " + lineNumber + ": " + e);
								}
								
								//condStream.close();
								byte[] condData = condStream.toByteArray();
								int condDataLen = condData.length;
								
								// write it all
								instructionCount++;
								memStream.write(commandToken);
								memStream.write(condDataLen >> 8);
								memStream.write(condDataLen & 255);
								memStream.write(condData, 0, condDataLen);
							}
							catch (Exception e)
							{
								System.out.println("Error: condition exception, line " + lineNumber + ": " + e);
							}
						}
						else
						{
							if (setsize == 0)
							{
								System.out.println("Error: condition missing statement, line " + lineNumber);
							}
							else
							{
								System.out.println("Error: condition contains extra segments, line " + lineNumber);
							}
							return null;
						}
						break;

					case CRToken.COMMAND_IF:
						{
							// if:((SourceType=SRC_DeviceMake,CompareType=CMP_Equal,MatchString="samsung")),(SourceType=,CompareType=,MatchString=),...])
							ArrayList<String> conditions = ParseSegments(RemoveSurrounds(line, "(", ")"), ',', "(\"", ")\"");

							int condLen = conditions.size();
							if (condLen == 0)
							{
								System.out.println("Error: if has no conditions, line " + lineNumber);
								return null;
							}
							else if (condLen > 255)
							{
								System.out.println("Error: if has too many conditions, line " + lineNumber);
							}

							try
							{
								ByteArrayOutputStream condStream = new ByteArrayOutputStream();

								// placeholder for later patching (false case jump)
								condStream.write(0);
								condStream.write(0);
								condStream.write(0);
								condStream.write(0);

								// deal with conditions parsing
								if (!parseConditions(condStream, conditions, lineNumber, bVerbose))
								{
									return null;
								}

								//condStream.close();
								byte[] condData = condStream.toByteArray();
								int condDataLen = condData.length;

								// write it all
								instructionCount++;
								memStream.write(commandToken);
								memStream.write(condDataLen >> 8);
								memStream.write(condDataLen & 255);

								// remember location for false fixup and save previous endifs
								ifDepth++;
								recoverOffsets.push(endifOffsets);
								endifOffsets = new Stack<Integer>();
								falseOffsets.push(falseOffset);
								falseOffset = memStream.size();
	
								memStream.write(condData, 0, condDataLen);
							}
							catch (Exception e)
							{
								System.out.println("Error: if exception, line " + lineNumber + ": " + e);
							}
						}
						break;

					case CRToken.COMMAND_ELSEIF:
						if (ifDepth > 0)
						{
							if (falseOffset == -1)
							{
								System.out.println("Error: elseif after else , line " + lineNumber);
								return null;
							}

							// write goto
							instructionCount++;
							memStream.write(CRToken.COMMAND_GOTO);
							memStream.write(0);
							memStream.write(4);
							// remember offset to fix up for endif
							endifOffsets.push(memStream.size());
							memStream.write(0);
							memStream.write(0);
							memStream.write(0);
							memStream.write(0);

							// elseif:((SourceType=SRC_DeviceMake,CompareType=CMP_Equal,MatchString="samsung")),(SourceType=,CompareType=,MatchString=),...])
							ArrayList<String> conditions = ParseSegments(RemoveSurrounds(line, "(", ")"), ',', "(\"", ")\"");

							int condLen = conditions.size();
							if (condLen == 0)
							{
								System.out.println("Error: elseif has no conditions, line " + lineNumber);
								return null;
							}
							else if (condLen > 255)
							{
								System.out.println("Error: elseif has too many conditions, line " + lineNumber);
							}

							try
							{
								ByteArrayOutputStream condStream = new ByteArrayOutputStream();

								// placeholder for later patching (false case jump)
								condStream.write(0);
								condStream.write(0);
								condStream.write(0);
								condStream.write(0);

								// deal with conditions parsing
								if (!parseConditions(condStream, conditions, lineNumber, bVerbose))
								{
									return null;
								}

								//condStream.close();
								byte[] condData = condStream.toByteArray();
								int condDataLen = condData.length;

								// write it all
								int here = memStream.size();
								instructionCount++;
								memStream.write(commandToken);
								memStream.write(condDataLen >> 8);
								memStream.write(condDataLen & 255);

								// save falseOffset goto fixup to here
								gotoFixups.put(falseOffset, here);
								falseOffset = memStream.size();

								memStream.write(condData, 0, condDataLen);
							}
							catch (Exception e)
							{
								System.out.println("Error: if exception, line " + lineNumber + ": " + e);
							}
						}
						else
						{
							System.out.println("Error: elseif without an if, line " + lineNumber);
							return null;
						}
						break;

					case CRToken.COMMAND_ELSE:
						if (ifDepth > 0)
						{
							if (falseOffset == -1)
							{
								System.out.println("Error: double else , line " + lineNumber);
								return null;
							}
							
							// write goto
							instructionCount++;
							memStream.write(CRToken.COMMAND_GOTO);
							memStream.write(0);
							memStream.write(4);
							// remember offset to fix up for endif
							endifOffsets.push(memStream.size());
							memStream.write(0);
							memStream.write(0);
							memStream.write(0);
							memStream.write(0);

							int here = memStream.size();

							// save falseOffset goto fixup to here
							if (falseOffset != -1)
							{
								gotoFixups.put(falseOffset, here);
							}
							// clear it since we're at the end of the chain
							falseOffset = -1;

							// this is a nop.. don't have to write it but makes decompile easier
							instructionCount++;
							memStream.write(commandToken);
							memStream.write(0);
							memStream.write(0);
						}
						else
						{
							System.out.println("Error: else without an if, line " + lineNumber);
							return null;
						}
						break;

					case CRToken.COMMAND_ENDIF:
						if (ifDepth > 0)
						{
							// position for false and true gotos
							int here = memStream.size();
							
							// save falseOffset goto for here
							if (falseOffset != -1)
							{
								gotoFixups.put(falseOffset, here);
							}
							// recover from any previous if
							falseOffset = falseOffsets.pop();

							// add goto fixups for endif
							while (!endifOffsets.empty())
							{
								gotoFixups.put(endifOffsets.pop(), here);
							}
							// recover from any previous if
							endifOffsets = recoverOffsets.pop();
							
							// this is a nop.. don't have to write it but makes decompile easier
							instructionCount++;
							memStream.write(commandToken);
							memStream.write(0);
							memStream.write(0);
							
							ifDepth--;
						}
						else
						{
							System.out.println("Error: endif without an if, line " + lineNumber);
							return null;
						}
						break;

					default:
						System.out.println("Error: unknown command '" + command +"' line " + lineNumber);
						return null;
				}
			}

			// makes sure last variable update is checked
			instructionCount++;
			memStream.write(CRToken.COMMAND_EOF);
			memStream.write(0);
			memStream.write(0);

			reader.close();
			
			//memStream.close();
			int memStreamLen = memStream.size();

			StringTable.close();
			int stringOffsetsLen = StringTable.stringOffsets.size();
			int stringLengthsLen = StringTable.stringLengths.size();
			int stringHeaderLen = stringOffsetsLen + stringLengthsLen;
			int stringDataLen = StringTable.stringData.size();
			int stringLen = stringHeaderLen + stringDataLen;
			int stringCount = StringTable.count();

			ByteArrayOutputStream dataStream = new ByteArrayOutputStream(2 + 2 + 2 + 2 + stringLen + 2 + memStreamLen);
			dataStream.write(0);	// indicate binary (anything before 0x20)
			dataStream.write(1);	// version
			dataStream.write(configRulesVersion >> 8);
			dataStream.write(configRulesVersion & 255);
			dataStream.write(stringCount >> 8);
			dataStream.write(stringCount & 255);
			dataStream.write(stringDataLen >> 8);
			dataStream.write(stringDataLen & 255);
			dataStream.write(StringTable.stringOffsets.toByteArray(), 0, stringOffsetsLen);
			dataStream.write(StringTable.stringLengths.toByteArray(), 0, stringLengthsLen);
			dataStream.write(StringTable.stringData.toByteArray(), 0, stringDataLen);
			dataStream.write(instructionCount >> 8);
			dataStream.write(instructionCount & 255);

			byte[] memStreamBytes = memStream.toByteArray();
			for (Map.Entry<Integer, Integer> entry : gotoFixups.entrySet())
			{
				int dest = entry.getKey();
				int offset = entry.getValue();
				
				memStreamBytes[dest++] = (byte)((offset >> 24) & 0xFF);
				memStreamBytes[dest++] = (byte)((offset >> 16) & 0xFF); 
				memStreamBytes[dest++] = (byte)((offset >> 8) & 0xFF); 
				memStreamBytes[dest] = (byte)(offset & 0xFF); 
			}
			dataStream.write(memStreamBytes, 0, memStreamLen);
			
			System.out.println("Instruction count: " + (instructionCount-1) + " from " + lineNumber + " lines");
			System.out.println("String count: " + stringCount + " from " + StringTable.calls);
			System.out.println("Regex removed: " + RegExRemoved + " of " + RegExFound);
			return dataStream.toByteArray();
		}
		catch (IOException ie)
		{
			System.out.println("failed to read configuration rules: " + ie);
			return null;
		}
		// unreachable
	}
	
	// =====


	public static void writeString(ByteArrayOutputStream stream, String output)
	{
		stream.write(output.getBytes(), 0, output.length());
		System.out.print(output);
	}

	public static void dumpConditions(ByteArrayInputStream2 byteread, ByteArrayOutputStream dataStream) throws IOException
	{
		writeString(dataStream,"(");
		int groups = byteread.read();
		while (groups-- > 0)
		{
			int sourceToken = byteread.read();
			String Source = StringTable.getString(byteread.readShort());
			int compareToken = byteread.read();
			String MatchString = StringTable.getString(byteread.readShort());

			if (sourceToken == CRToken.SOURCE_EXIST)
			{
				Source = "\"[EXIST]\"";
			}

			writeString(dataStream,"(SourceType=" + Source +",CompareType=" + CRToken.getCompare(compareToken) + ",MatchString=\"" + MatchString + "\")" + (groups > 0 ? "," : ""));
		}
		writeString(dataStream,")");
	}

	private static byte[] decompileConfigRules(byte[] sourceData, boolean bDebug)
	{
		try
		{
			ByteArrayOutputStream dataStream = new ByteArrayOutputStream();

			CRToken.Init();
			CRToken.InitStrings();

			ByteArrayInputStream2 byteread = new ByteArrayInputStream2(sourceData);
			
			int version = byteread.readShort();
			int configRulesVersion = byteread.readShort();
			writeString(dataStream, "// version:" + configRulesVersion + "\n");

			StringTable.Init(byteread);
			
			int commandToken;
			int commandLength = -1;

			int instructionCount = byteread.readShort();
			int position = byteread.getPosition();
			int basePosition = position;

			String curPos = "";
			int ifDepth = 0;

			final char indentChar = '\t';
			final int indentMult = 1;

			while (instructionCount-- > 0)
			{
				while (instructionCount-- > 0)
				{
					// move to next command
					byteread.seek(position);

					// read command and data length and advance position for next command
					int currentPosition = position - basePosition;
					commandToken = byteread.read();
					commandLength = byteread.readShort();
					position += 1 + 2 + commandLength;
					
					curPos = (bDebug ? "" + String.format("%5d:  ", currentPosition) : "") + new String(new char[ifDepth*indentMult]).replace('\0', indentChar);

					switch (commandToken)
					{
						case CRToken.COMMAND_EOF:
							instructionCount = 0;
							break;
							
						case CRToken.COMMAND_GOTO:
							{
								int offset = byteread.readInt();
								if (bDebug)
								{
									writeString(dataStream, curPos + ";goto: " + offset + "\n");
								}
							}
							break;
							
						case CRToken.COMMAND_SET:
							{
								writeString(dataStream, curPos + "set:(");
								int sets = byteread.read();
								while (sets-- > 0)
								{
									int flags = (int)byteread.read();
									String key = StringTable.getString(byteread.readShort());
									String value = StringTable.getString(byteread.readShort());
									if ((flags & CRToken.SETVARFLAGS_APPEND) > 0)
									{
										key = "APPEND_" + key;
									}
									writeString(dataStream, key + "=\"" + value + (sets > 0 ? "\"," : "\""));
								}
								writeString(dataStream, ")\n");
							}
							break;
							
						case CRToken.COMMAND_CLEAR:
							{
								writeString(dataStream, curPos + "clear:(");
								int clears = byteread.read();
								while (clears-- > 0)
								{
									String key = StringTable.getString(byteread.readShort());
									writeString(dataStream, key + (clears > 0 ? "," : ""));
								}
								writeString(dataStream, ")\n");
							}
							break;

						case CRToken.COMMAND_CHIPSET:
							{
								writeString(dataStream, curPos + "chipset:");
								String key = StringTable.getString(byteread.readShort());
								String useAffinity = StringTable.getString(byteread.readShort());
								String chipset = StringTable.getString(byteread.readShort());;
								String gpu = StringTable.getString(byteread.readShort());
								String processorCount = StringTable.getString(byteread.readShort());
								String bigCoreMask = StringTable.getString(byteread.readShort());
								String littleCoreMask = StringTable.getString(byteread.readShort());
								String deltas = StringTable.getString(byteread.readShort());

								if (deltas.length() > 0)
								{
									char[] keyArray = key.toCharArray();
									int currentIndex = 0;
									for (int index=0; index < deltas.length(); index++)
									{
										currentIndex += deltas.charAt(index);
										keyArray[currentIndex] = Character.toUpperCase(keyArray[currentIndex]);
									}
									key = String.valueOf(keyArray);
								}

								writeString(dataStream, "\"" + key + "\", ");
								writeString(dataStream, useAffinity + ", ");
								writeString(dataStream, "\"" + chipset + "\", ");
								writeString(dataStream, "\"" + gpu + "\", ");
								writeString(dataStream, processorCount + ", ");
								writeString(dataStream, bigCoreMask + ", ");
								writeString(dataStream, littleCoreMask + "\n");
							}
							break;

						case CRToken.COMMAND_CONDITION:
							{
								writeString(dataStream, curPos + "condition:");

								dumpConditions(byteread, dataStream);

								int sets = byteread.read();
								if (sets > 0)
								{
									writeString(dataStream, ", (");
									while (sets-- > 0)
									{
										int flags = (int)byteread.read();
										String key = StringTable.getString(byteread.readShort());
										String value = StringTable.getString(byteread.readShort());
										if ((flags & CRToken.SETVARFLAGS_APPEND) > 0)
										{
											key = "APPEND_" + key;
										}
										writeString(dataStream, key + "=\"" + value + (sets > 0 ? "\"," : "\""));
									}
									writeString(dataStream, ")");
								}
								else
								{
									writeString(dataStream, ",");
								}
								int clears = byteread.read();
								if (clears > 0)
								{
									writeString(dataStream, ", (");
									while (clears-- > 0)
									{
										String key = StringTable.getString(byteread.readShort());
										writeString(dataStream, key + (clears > 0 ? "," : ""));
									}
									writeString(dataStream, ")");
								}
								writeString(dataStream, "\n");
							}
							break;

						case CRToken.COMMAND_IF:
							{
								int offsetFalse = byteread.readInt();
								ifDepth++;

								writeString(dataStream, curPos + "if:");
								dumpConditions(byteread, dataStream);
								if (bDebug)
								{
									writeString(dataStream, "\n;   false: " + offsetFalse);
								}
								writeString(dataStream, "\n");
							}
							break;

						case CRToken.COMMAND_ELSEIF:
							{
								int offsetFalse = byteread.readInt();

								writeString(dataStream, curPos.substring(0, curPos.length() - indentMult) + "elseif:");
								dumpConditions(byteread, dataStream);
								if (bDebug)
								{
									writeString(dataStream, "\n;   false: " + offsetFalse);
								}
								writeString(dataStream, "\n");
							}
							break;

						case CRToken.COMMAND_ELSE:
							{
								writeString(dataStream, curPos.substring(0, curPos.length() - indentMult) + "else:");
								writeString(dataStream, "\n");
							}
							break;

						case CRToken.COMMAND_ENDIF:
							{
								writeString(dataStream, curPos.substring(0, curPos.length() - indentMult) + "endif:");
								writeString(dataStream, "\n");
								ifDepth--;
							}
							break;
							
						default:
							break;
					}
				}
			}
			
			return dataStream.toByteArray();
		}
		catch (IOException ie)
		{
			System.out.println("failed to decompile configuration rules: " + ie);
			return null;
		}
		// unreachable
	}

}