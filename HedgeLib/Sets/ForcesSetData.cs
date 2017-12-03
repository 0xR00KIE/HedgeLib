﻿using HedgeLib.Headers;
using HedgeLib.IO;
using System;
using System.Collections.Generic;
using System.IO;

namespace HedgeLib.Sets
{
    // Huge thanks to SuperSonic16 for his help on reverse-engineering the Forces set formats!
    public class ForcesSetData : SetData
    {
        // Variables/Constants
        public List<SetObjectType> Types = new List<SetObjectType>();
        public BINAHeader Header = new BINAHeader();
        public const string Extension = ".gedit";

        // Methods
        public override void Load(Stream fileStream,
            Dictionary<string, SetObjectType> objectTemplates)
        {
            // BINA Header
            var reader = new BINAReader(fileStream, BINA.BINATypes.Version2);
            Header = reader.ReadHeader();

            // Set Data Header
            ulong padding1 = reader.ReadUInt64();
            ulong padding2 = reader.ReadUInt64();

            long objectTableOffset = reader.ReadInt64();
            ulong objectCount = reader.ReadUInt64();
            ulong objectCount2 = reader.ReadUInt64();
            ulong padding3 = reader.ReadUInt64();

            // Padding/Unknown Value Checks
            if (padding1 != 0)
                Console.WriteLine($"WARNING: Padding1 != 0 ({padding1})");

            if (padding2 != 0)
                Console.WriteLine($"WARNING: Padding2 != 0 ({padding2})");

            if (padding3 != 0)
                Console.WriteLine($"WARNING: Padding3 != 0 ({padding3})");

            if (objectCount != objectCount2)
            {
                Console.WriteLine(
                    "WARNING: ObjectCount ({0}) != ObjectCount2 ({1})",
                    objectCount, objectCount2);
            }

            // Object Offsets
            var objectOffsets = new long[objectCount];
            reader.JumpTo(objectTableOffset, false);

            for (uint i = 0; i < objectCount; ++i)
            {
                objectOffsets[i] = reader.ReadInt64();
            }

            // Objects
            for (uint i = 0; i < objectCount; ++i)
            {
                reader.JumpTo(objectOffsets[i], false);

                var obj = ReadObject(reader, objectTemplates);
                if (obj == null) continue;
                Objects.Add(obj);
            }
        }

        protected SetObject ReadObject(BINAReader reader,
            Dictionary<string, SetObjectType> objectTemplates)
        {
            var obj = new SetObject();
            ulong padding1 = reader.ReadUInt64();
            long objTypeOffset = reader.ReadInt64();
            long objNameOffset = reader.ReadInt64();

            ushort id = reader.ReadUInt16(); // ?
            ushort unknown1 = reader.ReadUInt16();
            ushort parentID = reader.ReadUInt16();
            ushort parentUnknown1 = reader.ReadUInt16();
            obj.CustomData.Add("ParentID", new SetObjectParam(
                typeof(ushort), parentID));

            obj.ObjectID = id;

            var pos = reader.ReadVector3();
            var rot = reader.ReadVector3(); // ?
            var pos2 = reader.ReadVector3(); // TODO: Find out what this is for
            var rot2 = reader.ReadVector3(); // TODO: Find out what this is for

            obj.Transform.Position = pos;
            //obj.Transform.Rotation = rot; // TODO: Set rotation

            long extraParamsOffset = reader.ReadInt64();
            ulong unknownCount1 = reader.ReadUInt64(); // TODO: Is this a ulong?
            ulong unknownCount2 = reader.ReadUInt64(); // TODO: Is this a ulong?
            ulong padding3 = reader.ReadUInt64();

            long objParamsOffset = reader.ReadInt64();

            // Unknown Count Checks
            if (unknownCount1 != unknownCount2)
            {
                Console.WriteLine(
                    "WARNING: UnknownCount1 ({0}) != UnknownCount2 ({1})",
                    unknownCount1, unknownCount2);
            }

            if (unknownCount1 > 1)
            {
                Console.WriteLine(
                    "WARNING: UnknownCount1 > 1 ({0})",
                    unknownCount1);
            }

            // Extra Parameter Offsets
            var extraParamOffsets = new long[unknownCount1];
            reader.JumpTo(extraParamsOffset, false);

            for (uint i = 0; i < unknownCount1; ++i)
            {
                extraParamOffsets[i] = reader.ReadInt64();
                ulong padding5 = reader.ReadUInt64(); // TODO: Make sure this is correct
            }

            // Extra Parameters
            for (uint i = 0; i < unknownCount1; ++i)
            {
                reader.JumpTo(extraParamOffsets[i], false);
                ulong padding6 = reader.ReadUInt64();

                long extraParamNameOffset = reader.ReadInt64();
                ulong extraParamLength = reader.ReadUInt64();
                long extraParamDataOffset = reader.ReadInt64();

                // Extra Parameter Data
                reader.JumpTo(extraParamDataOffset, false);
                var data = reader.ReadBytes((int)extraParamLength);

                // Extra Parameter Name
                reader.JumpTo(extraParamNameOffset, false);
                string name = reader.ReadNullTerminatedString();

                // Parse Data
                switch (name)
                {
                    case "RangeSpawning":
                    {
                        obj.CustomData.Add("RangeIn", new SetObjectParam(
                            typeof(float), BitConverter.ToSingle(data, 0)));

                        obj.CustomData.Add("RangeOut", new SetObjectParam(
                            typeof(float), BitConverter.ToSingle(data, 4)));
                        continue;
                    }
                }

                Console.WriteLine($"WARNING: Unknown extra parameter type {name}");
                obj.CustomData.Add(name, new SetObjectParam(
                    data.GetType(), data));
            }

            // Object Name
            reader.JumpTo(objNameOffset, false);
            string objName = reader.ReadNullTerminatedString();
            obj.CustomData.Add("Name", new SetObjectParam(typeof(string), objName));

            // Object Type
            reader.JumpTo(objTypeOffset, false);
            string objType = reader.ReadNullTerminatedString();
            obj.ObjectType = objType;

            if (!objectTemplates.ContainsKey(objType))
            {
                // TODO: Un-comment this before committing!
                Console.WriteLine(
                    "WARNING: Skipped {0} because there is no template for type {1}!",
                    objName, objType);
                Console.WriteLine("Params at: {0:X}",
                    objParamsOffset + reader.Offset);
                return null;
            }

            var template = objectTemplates[objType];

            // Object Parameters
            reader.JumpTo(objParamsOffset, false);
            foreach (var param in template.Parameters)
            {
                // TODO: Figure out how arrays and special values work

                // Special Param Types
                if (param.DataType == typeof(uint[]))
                {
                    // TODO: Make sure all of this is right, and make it so
                    // you can write these as well
                    reader.FixPadding(4);
                    long arrOffset = reader.ReadInt64();
                    ulong arrLength = reader.ReadUInt64();
                    ulong arrLength2 = reader.ReadUInt64();
                    long curPos = reader.BaseStream.Position;

                    if (arrLength != arrLength2)
                    {
                        Console.WriteLine(
                            "WARNING: ArrLength ({0}) != ArrLength2 ({1})",
                            arrLength, arrLength2);
                    }

                    var arr = new uint[arrLength];
                    reader.JumpTo(arrOffset, false);

                    for (uint i = 0; i < arrLength; ++i)
                    {
                        arr[i] = reader.ReadUInt32();
                    }

                    obj.Parameters.Add(new SetObjectParam(
                        typeof(uint[]), arr));

                    reader.JumpTo(curPos);
                    continue;
                }

                // Pre-Param Padding
                if (param.DataType == typeof(float) ||
                    param.DataType == typeof(uint) || param.DataType == typeof(int))
                {
                    reader.FixPadding(4);
                }
                else if (param.DataType == typeof(double) ||
                    param.DataType == typeof(ulong) || param.DataType == typeof(long))
                {
                    reader.FixPadding(8);
                }

                var objParam = new SetObjectParam(param.DataType,
                    reader.ReadByType(param.DataType));
                obj.Parameters.Add(objParam);

                // Post-Param Padding
                if (param.DataType == typeof(Vector3))
                {
                    reader.FixPadding(16); // TODO: Is this correct?
                }
            }

            // Padding Checks
            if (padding1 != 0)
                Console.WriteLine($"WARNING: Obj Padding1 != 0 ({padding1})");

            if (padding3 != 0)
                Console.WriteLine($"WARNING: Obj Padding3 != 0 ({padding3})");

            return obj;
        }

        public override void Save(Stream fileStream)
        {
            // BINA Header
            var writer = new BINAWriter(fileStream,
                BINA.BINATypes.Version2, false);

            Header.Version = 210;

            // Set Data Header
            writer.Write(0UL);
            writer.Write(0UL);

            writer.AddOffset("objectTableOffset", 8);
            writer.Write((ulong)Objects.Count);
            writer.Write((ulong)Objects.Count);
            writer.Write(0UL);

            // Objects
            writer.FillInOffsetLong("objectTableOffset", false, false);
            writer.AddOffsetTable("objectOffset", (uint)Objects.Count, 8);

            for (int i = 0; i < Objects.Count; ++i)
            {
                var obj = Objects[i];
                writer.FillInOffsetLong($"objectOffset_{i}", false, false);
                WriteObject(writer, obj, i);
            }

            writer.FixPadding(16);

            // Object Parameters
            for (int i = 0; i < Objects.Count; ++i)
            {
                writer.FixPadding(16);
                WriteObjectParameters(writer, Objects[i], i);
            }

            writer.Write(0U); // TODO: Is this required?
            writer.FixPadding(4);
            writer.FinishWrite(Header);
        }

        protected void WriteObject(BINAWriter writer, SetObject obj, int objID)
        {
            writer.Write(0UL);

            // Object Type
            writer.AddString($"objTypeOffset{objID}", obj.ObjectType, 8);

            // Object Name
            string name = "";
            if (obj.CustomData.ContainsKey("Name"))
                name = (obj.CustomData["Name"].Data as string);

            if (string.IsNullOrEmpty(name))
                name = $"{obj.ObjectType}{objID}";

            writer.AddString($"objNameOffset{objID}", name, 8);

            // Object Entry
            writer.Write((ushort)obj.ObjectID);
            writer.Write((ushort)0);
            writer.Write((obj.CustomData.ContainsKey("ParentID")) ?
                (ushort)obj.CustomData["ParentID"].Data : (ushort)0);
            writer.Write((ushort)0);

            writer.Write(obj.Transform.Position);
            writer.Write(new Vector3(0, 0, 0)); // TODO: Write actual rotation data
            writer.Write(obj.Transform.Position);
            writer.Write(new Vector3(0, 0, 0)); // TODO: Write actual rotation data

            writer.AddOffset($"extraParamsOffset{objID}", 8);
            writer.Write(1UL); // TODO
            writer.Write(1UL); // TODO
            writer.Write(0UL);
            writer.AddOffset($"objParamsOffset{objID}", 8);
            writer.FixPadding(16);

            // Extra Parameter Entries
            uint extraParamCounts = (uint)obj.CustomData.Count;
            if (obj.CustomData.ContainsKey("Name"))
                extraParamCounts -= 1;

            writer.FillInOffsetLong($"extraParamsOffset{objID}", false, false);
            writer.AddOffsetTable($"extraParamOffset{objID}", extraParamCounts, 8);
            writer.FixPadding(16); // TODO: Make sure this is correct

            int i = -1;
            foreach (var customData in obj.CustomData)
            {
                if (customData.Key == "Name" || customData.Key == "ParentID" ||
                    customData.Key == "RangeOut")
                    continue;

                writer.FillInOffsetLong(
                    $"extraParamOffset{objID}_{++i}", false, false);

                writer.Write(0UL);
                writer.AddString($"extraParamNameOffset{objID}{i}",
                    (customData.Key == "RangeIn") ? "RangeSpawning" : customData.Key, 8);

                if (!WriteExtraParamLength(writer, customData))
                {
                    writer.Write(0);
                    Console.WriteLine(
                        $"WARNING: CustomData {customData.Key} skipped; Unknown Type!");
                }
                
                writer.AddOffset($"extraParamDataOffset{objID}{i}", 8);
            }

            // Extra Parameter Data
            foreach (var customData in obj.CustomData)
            {
                if (customData.Key == "Name" || customData.Key == "RangeOut")
                    continue;

                writer.FillInOffsetLong(
                    $"extraParamDataOffset{objID}{i}", false, false);

                if (!WriteExtraParamData(writer, obj, customData))
                    writer.Write(0UL);
            }
        }

        protected void WriteObjectParameters(BINAWriter writer,
            SetObject obj, int objID)
        {
            writer.FillInOffsetLong($"objParamsOffset{objID}", false, false);
            foreach (var param in obj.Parameters)
            {
                if (param.DataType == typeof(float) ||
                    param.DataType == typeof(uint) || param.DataType == typeof(int))
                {
                    writer.FixPadding(4);
                }
                else if (param.DataType == typeof(double) ||
                    param.DataType == typeof(ulong) || param.DataType == typeof(long))
                {
                    writer.FixPadding(8);
                }

                // TODO: Write special types
                writer.WriteByType(param.DataType, param.Data);

                if (param.DataType == typeof(Vector3))
                {
                    writer.FixPadding(16); // TODO: Is this correct?
                }
            }
        }

        protected bool WriteExtraParamLength(BINAWriter writer,
            KeyValuePair<string, SetObjectParam> customData)
        {
            switch (customData.Key)
            {
                case "RangeIn":
                    writer.Write(8UL);
                    return true;
            }

            return false;
        }

        protected bool WriteExtraParamData(
            BINAWriter writer, SetObject obj,
            KeyValuePair<string, SetObjectParam> customData)
        {
            var param = customData.Value;
            switch (customData.Key)
            {
                case "RangeIn":
                    writer.Write((float)param.Data);
                    writer.Write((float)obj.CustomData["RangeOut"].Data);
                    return true;
            }

            return false;
        }
    }
}