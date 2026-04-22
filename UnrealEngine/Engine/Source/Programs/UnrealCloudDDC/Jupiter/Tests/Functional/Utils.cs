// Copyright Epic Games, Inc. All Rights Reserved.

using System.Net;
using System.Net.Http;
using System.Text.Json;
using System.Threading.Tasks;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Jupiter.Tests.Functional
{
	public static class JsonTestUtils
	{
		public static readonly JsonSerializerOptions DefaultJsonSerializerSettings = ConfigureJsonOptions();

		private static JsonSerializerOptions ConfigureJsonOptions()
		{
			JsonSerializerOptions options = new JsonSerializerOptions();
			BaseStartup.ConfigureJsonOptions(options);
			return options;
		}
	}

	public static class HttpResponseMessageExtensions
	{
		public static async Task EnsureSuccessStatusCodeWithMessageAsync(this HttpResponseMessage response)
		{
			if (response.StatusCode == HttpStatusCode.InternalServerError)
			{
				Assert.Fail($"Internal server error with message: {await response.Content.ReadAsStringAsync()}");
			}

			response.EnsureSuccessStatusCode();
		}
	}
}
