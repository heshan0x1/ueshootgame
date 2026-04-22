#
# GDB Printers for the Unreal Engine 4
#
# How to install:
# If the file ~/.gdbinit doesn't exist
#        touch ~/.gdbinit
#        open ~/.gdbinit
#
# and add the following lines:
#   python
#   import sys
#   ...
#   sys.path.insert(0, '/Path/To/Epic/UE/Engine/Extras/GDBPrinters')      <--
#   ...
#   from UEPrinters import register_ue_printers                          <--
#   register_ue_printers (None)                                           <--
#   ...
#   end


import itertools
import random
import re
import sys

import gdb

# ------------------------------------------------------------------------------
# We make our own base of the iterator to prevent issues between Python 2/3.
#

if sys.version_info[0] == 3:
	Iterator = object
else:
	class Iterator(object):

		def next(self):
			return type(self).__next__(self)

# ------------------------------------------------------------------------------
# Helper functions
#

def default_iterator(val):
	for field in val.type.fields():
		yield field.name, val[field.name]

def container_iterator(Array):
	for i in range(Array.ArrayNum):
		if Array.is_valid(i):
			yield ('[%d]' % i, Array.get_value(i))

def get_allocation_data(Allocator, ElementType):
	Data = None
	try:
		Data = Allocator['Data']
	except:
		pass

	if (Data == None):
		try:
			SecondaryData = Allocator['SecondaryData']
			if SecondaryData != None:
				SecondaryDataData = SecondaryData['Data']
				if (SecondaryDataData > 0):
					Data = SecondaryDataData
		except:
			pass

	if (Data == None):
		try:
			Data = Allocator['InlineData']
		except:
			pass

	if (Data == None):
		return 0

	return Data.cast(ElementType.pointer())

# ------------------------------------------------------------------------------
#
#  Custom pretty printers.
#
#


# ------------------------------------------------------------------------------
# FBitReference
#
class FBitReferencePrinter:
	def __init__(self, val):
		self.Value = val

	def to_string(self):
		self.Mask = self.Value['Mask']
		self.Data = self.Value['Data']
		return '\'%d\'' % (self.Data & self.Mask)

# ------------------------------------------------------------------------------
# TBitArray
#
class TBitArrayPrinter:
	"Print TBitArray"

	def __init__(self, val):
		self.Value = val
		self.ArrayNum = self.Value['NumBits']
		self.ArrayMax = self.Value['MaxBits']
		self.Data = get_allocation_data(self.Value['AllocatorInstance'], gdb.lookup_type("uint32"))

	def to_string(self):
		if self.ArrayNum.is_optimized_out:
			pass
		if self.ArrayNum == 0:
			return 'empty'
		return 'Num='+str(self.ArrayNum)+' Max='+str(self.ArrayMax)

	def children(self):
		return container_iterator(self)

	def display_hint(self):
		return 'array'
	
	def get_value(self, index):
		return ((self.Data[index/32] >> index) & 1) != 0
		
	def is_valid(self, index):
		return index < self.ArrayMax

# ------------------------------------------------------------------------------
# TIndirectArray
#


# ------------------------------------------------------------------------------
# TChunkedArray
#
class TChunkedArrayPrinter:
	"Print TChunkedArray"

	class _iterator(Iterator):
		def __init__(self, val):
			self.Value = val
			self.Counter = -1
			self.ElementType = self.Value.type.template_argument(0)
			self.ElementTypeSize = self.ElementType.sizeof
			self.NumElementsPerChunk = self.Value.type.template_argument(1)/self.ElementTypeSize

			try:
				self.NumElements = self.Value['NumElements']
				if self.NumElements.is_optimized_out:
					self.NumElements = 0
				else:
					self.Chunks = self.Value['Chunks']
					self.Array = self.Chunks['Array']
					self.ArrayNum = self.Array['ArrayNum']
					self.AllocatorInstance = self.Array['AllocatorInstance']
					self.AllocatorData = self.AllocatorInstance['Data']
			except:
				raise

		def __iter__(self):
			return self

		def __next__(self):
			return self.next()

		def __next__(self):
			if self.NumElements == 0:
				raise StopIteration

			self.Counter = self.Counter + 1

			if self.Counter >= self.NumElements:
				raise StopIteration()

			Expr = '*(*((('+str(self.ElementType.name)+'**)'+str(self.AllocatorData)+')+'+str(self.Counter / self.NumElementsPerChunk)+')+'+str(self.Counter % self.NumElementsPerChunk)+')'
			Val = gdb.parse_and_eval(Expr)
			return ('[%d]' % self.Counter, Val)

	def __init__(self, val):
		self.Value = val
		self.NumElements = self.Value['NumElements']

	def to_string(self):
		if self.NumElements.is_optimized_out:
			pass
		if self.NumElements == 0:
			return 'empty'
		pass

	def children(self):
		return self._iterator(self.Value)

	def display_hint(self):
		return 'array'


# ------------------------------------------------------------------------------
# TArray
#
class TArrayPrinter:
	"Print TArray"

	def __init__(self, val):
		self.Value = val
		self.ArrayNum = self.Value['ArrayNum']
		self.ArrayMax = self.Value['ArrayMax']
		self.Data = get_allocation_data(self.Value['AllocatorInstance'], self.Value.type.template_argument(0))

	def to_string(self):
		if self.ArrayNum.is_optimized_out:
			pass
		if self.ArrayNum == 0:
			return 'empty'
		return 'Num='+str(self.ArrayNum)+' Max='+str(self.ArrayMax)

	def children(self):
		return container_iterator(self)

	def display_hint(self):
		return 'array'

	def get_value(self, index):
		return self.Data[index]
		
	def is_valid(self, index):
		return index < self.ArrayMax

# ------------------------------------------------------------------------------
# TSparseArray
#
class TSparseArrayPrinter:
	"Print TSparseArray"

	def __init__(self, val):
		self.Value = val
		self.Array = TArrayPrinter(self.Value['Data'])
		self.AllocationFlags = TBitArrayPrinter(self.Value['AllocationFlags'])

		self.NumFreeIndices = self.Value['NumFreeIndices']
		self.ArrayNum = self.Array.ArrayNum
		self.ArrayMax = self.Array.ArrayMax
		self.Data = self.Array.Data.cast(self.Value.type.template_argument(0).pointer())

	def to_string(self):
		if self.ArrayNum.is_optimized_out:
			pass
		if self.ArrayNum == 0:
			return 'empty'
		return 'Num='+str(self.ArrayNum - self.NumFreeIndices)+' Max='+str(self.ArrayMax)

	def children(self):
		return container_iterator(self)

	def display_hint(self):
		return 'array'

	def get_value(self, index):
		return self.Data[index]

	def is_valid(self, index):
		return self.AllocationFlags.get_value(index)

# ------------------------------------------------------------------------------
# TSet
#
class TSparseSetPrinter(TSparseArrayPrinter):
	"Print TSparseSet"

	def __init__(self, val):
		super().__init__(val['Elements'])

	def children(self):
		for Str, Element in super().children():
			yield (Str, Element['Value'])

# ------------------------------------------------------------------------------
# TSparseMap
#

class TSparseMapPrinter(TSparseSetPrinter):
	"Print TSparseMap"

	def __init__(self, val):
		super().__init__(val['Pairs'])

	def children(self):
		for Str, Pair in super().children():
			yield (f'[{Pair['Key']}] = {Pair['Value'].type}', Pair['Value'])

# ------------------------------------------------------------------------------
# TWeakObjectPtr
#

class TWeakObjectPtrPrinter:
	"Print TWeakObjectPtr"

	class _iterator(Iterator):
		def __init__(self, val):
			self.Value = val
			self.Counter = 0
			self.Object = None

			self.ObjectSerialNumber = int(self.Value['ObjectSerialNumber'])
			if self.ObjectSerialNumber >= 1:
				ObjectIndexValue = int(self.Value['ObjectIndex'])
				ObjectItemExpr = 'GCoreObjectArrayForDebugVisualizers->Objects['+str(ObjectIndexValue)+'/FChunkedFixedUObjectArray::NumElementsPerChunk]['+str(ObjectIndexValue)+ '% FChunkedFixedUObjectArray::NumElementsPerChunk]'
				ObjectItem = gdb.parse_and_eval(ObjectItemExpr);
				IsValidObject = int(ObjectItem['SerialNumber']) == self.ObjectSerialNumber
				if IsValidObject == True:
					ObjectType = self.Value.type.template_argument(0)
					self.Object = ObjectItem['Object'].dereference().cast(ObjectType.reference())

		def __iter__(self):
			return self

		def __next__(self):
			if self.Counter > 0:
				raise StopIteration

			self.Counter = self.Counter + 1

			if self.Object != None:
				return ('Object', self.Object)
			elif self.ObjectSerialNumber > 0:
				return ('Object', 'STALE')
			else:
				return ('Object', 'nullptr')


	def __init__(self, val):
		self.Value = val

	def children(self):
		return self._iterator(self.Value)

	def to_string(self):
		ObjectType = self.Value.type.template_argument(0)
		return 'TWeakObjectPtr<%s>' % ObjectType.name;


# ------------------------------------------------------------------------------
# FString
#
class FStringPrinter:
	"Print FString"

	def __init__(self, val):
		self.Value = val

	def to_string(self):
		if self.Value.is_optimized_out:
			return '<optimized out>'

		ArrayNum = self.Value['Data']['ArrayNum']
		if ArrayNum == 0:
			return 'empty'
		elif ArrayNum < 0:
			return "nullptr"
		else:
			ActualData = self.Value['Data']['AllocatorInstance']['Data']
			data = ActualData.cast(gdb.lookup_type("TCHAR").pointer())
			return '%s' % (data.string())

	def display_hint (self):
		return 'string'

# ------------------------------------------------------------------------------
# FName shared 
#
def lookup_fname_entry(id):
	expr = '((FNameEntry&)GNameBlocksDebug[%d >> FNameDebugVisualizer::OffsetBits][FNameDebugVisualizer::EntryStride * (%d & FNameDebugVisualizer::OffsetMask)])' % (id, id)
	return gdb.parse_and_eval(expr)

def get_fname_entry_string(entry):
	header = entry['Header']
	len = int(header['Len'].cast(gdb.lookup_type('uint16')))
	is_wide = header['bIsWide'].cast(gdb.lookup_type('bool'))
	if is_wide:
		wide_string = entry['WideName'].cast(gdb.lookup_type('WIDECHAR').pointer())
		return str(wide_string.string('','',len))
	else:
		ansi_string = entry['AnsiName'].cast(gdb.lookup_type('ANSICHAR').pointer())
		return str(ansi_string.string('','',len))

def get_fname_string(entry, number):
	if number == 0:
		return "'%s'" % get_fname_entry_string(entry)
	else:
		return "'%s'_%u" % (get_fname_entry_string(entry), number - 1)

# ------------------------------------------------------------------------------
# FNameEntry
#

class FNameEntryPrinter:
	"Print FNameEntry"

	def __init__(self, val):
		self.Value = val

	def to_string(self):
		header = self.Value['Header']
		len = int(header['Len'].cast(gdb.lookup_type('uint16')))
		if len == 0:
			base_entry  = lookup_fname_entry(int(self.Value['NumberedName']['Id']['Value']))
			number = self.Value['NumberedName']['Number']
			return get_fname_string(base_entry, number)
		else:
			return get_fname_string(self.Value, 0)

# ------------------------------------------------------------------------------
# FNameEntryId
#

class FNameEntryIdPrinter:
	"Print FNameEntryId"

	def __init__(self, val):
		self.Value = val

	def to_string(self):
		if self.Value.is_optimized_out:
			return '<optimized out>'
		id = int(self.Value['Value'])
		unused_mask = gdb.parse_and_eval('FNameDebugVisualizer::UnusedMask')
		if (id & unused_mask) != 0:
			return 'invalid'
		return lookup_fname_entry(id)


# ------------------------------------------------------------------------------
# FName
#
class FNamePrinter:
	"Print FName"

	def __init__(self, id, number):
		self.Id = id
		self.Number = number

	def to_string(self):
		return get_fname_string(lookup_fname_entry(self.Id), self.Number)


def make_fname_printer(val):
	if val.is_optimized_out:
		return '<optimized out>'
	id = int(val['ComparisonIndex']['Value'])
	unused_mask = gdb.parse_and_eval('FNameDebugVisualizer::UnusedMask')
	if (id & unused_mask) != 0:
		return 'invalid'
	
	# We need to pick the right printer based on whether FName has a Number member or not
	if gdb.types.has_field(val.type, "Number"):
		return FNamePrinter(id, int(val['Number']))
	else:
		# look up the id and use the FNameEntry printer
		return FNameEntryPrinter(lookup_fname_entry(id))
	

# ------------------------------------------------------------------------------
# FMinimalName
#
def make_fminimalname_printer(val):
	if val.is_optimized_out:
		return '<optimized out>'
	id = int(val['Index']['Value'])
	unused_mask = gdb.parse_and_eval('FNameDebugVisualizer::UnusedMask')
	if (id & unused_mask) != 0:
		return 'invalid'
	
	# We need to pick the right printer based on whether FName has a Number member or not
	if gdb.types.has_field(val.type, "Number"):
		return FNamePrinter(id, int(val['Number']))
	else:
		# look up the id and use the FNameEntry printer
		return FNameEntryIdPrinter(val['Index'])

# ------------------------------------------------------------------------------
# FPrimaryAssetId
#
def remove_quotes(name):
	return str(name).strip("'").strip('"')

class FPrimaryAssetIdPrinter:
	"Print FPrimaryAssetId"
	
	def __init__(self, val):
		self.Value = val

	def to_string(self):
		return remove_quotes(self.Value["PrimaryAssetType"]["Name"]) + ':' + remove_quotes(self.Value["PrimaryAssetName"])

# ------------------------------------------------------------------------------
# TPair (TTuple<Key,Value>)
#
class TPairPrinter:
	"Print TPair"
	
	def __init__(self, val):
		self.Value = val

	def children(self):
		yield ("Key", self.Value["Key"])
		yield ("Value", self.Value["Value"])

#
# Register our lookup function. If no objfile is passed use all globally.
def register_ue_printers(objfile):
	if objfile == None:
		objfile = gdb.current_objfile()
	gdb.printing.register_pretty_printer(objfile, build_ue_pretty_printer(), True)
	print("Registered pretty printers for UE classes")

def build_ue_pretty_printer():
	# add a random numeric suffix to the printer name so we can reload printers during the same session for iteration
	pp = gdb.printing.RegexpCollectionPrettyPrinter("UnrealEngine")
	pp.add_printer("FString", '^FString$', FStringPrinter)
	pp.add_printer("FNameEntry", '^FNameEntry$', FNameEntryPrinter)
	pp.add_printer("FNameEntryId", '^FNameEntryId$', FNameEntryIdPrinter)
	pp.add_printer("FName", '^FName$', make_fname_printer)
	pp.add_printer("FMinimalName", '^FMinimalName$', make_fminimalname_printer)
	pp.add_printer("TArray", '^TArray<.+,.+>$', TArrayPrinter)
	pp.add_printer("TBitArray", '^TBitArray<.+>$', TBitArrayPrinter)
	pp.add_printer("TChunkedArray", '^TChunkedArray<.+>$', TChunkedArrayPrinter)
	pp.add_printer("TSparseArray", '^TSparseArray<.+>$', TSparseArrayPrinter)
	pp.add_printer("TSet", '^TSet<.+>$', TSparseSetPrinter)
	pp.add_printer("TSparseSet", '^TSparseSet<.+>$', TSparseSetPrinter)
	pp.add_printer("FBitReference", '^FBitReference$', FBitReferencePrinter)
	pp.add_printer("TMapBase", '^TMapBase<.+,.+,.+,.+>$', TSparseMapPrinter)
	pp.add_printer("TSortableMapBase", '^TSortableMapBase<.+,.+,.+,.+>$', TSparseMapPrinter)
	pp.add_printer("TMap", '^TMap<.+,.+,.+,.+>$', TSparseMapPrinter)
	pp.add_printer("TSparseMapBase", '^TSparseMapBase<.+,.+,.+,.+>$', TSparseMapPrinter)
	pp.add_printer("TSortableSparseMapBase", '^TSortableSparseMapBase<.+,.+,.+,.+>$', TSparseMapPrinter)
	pp.add_printer("TSparseMap", '^TSparseMap<.+,.+,.+,.+>$', TSparseMapPrinter)
	pp.add_printer("TPair", '^TPair<.+,.+>$', TPairPrinter)
	pp.add_printer("TTuple", '^TTuple<.+,.+>$', TPairPrinter)
	pp.add_printer("TWeakObjectPtr", '^TWeakObjectPtr<.+>$', TWeakObjectPtrPrinter)
	pp.add_printer("FPrimaryAssetId", '^FPrimaryAssetId$', FPrimaryAssetIdPrinter)
	return pp

register_ue_printers(None)
