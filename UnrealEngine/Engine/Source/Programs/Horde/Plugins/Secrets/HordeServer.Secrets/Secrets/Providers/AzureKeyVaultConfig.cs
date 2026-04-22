// Copyright Epic Games, Inc. All Rights Reserved.

namespace HordeServer.Secrets.Providers;

/// <summary>
/// Configuration for Azure Key Vault secrets.
/// </summary>
public class AzureKeyVaultConfig
{
	/// <summary>
	/// The Uri to the Azure Key Vault.
	/// </summary>
	public string? VaultUri { get; set; }
}