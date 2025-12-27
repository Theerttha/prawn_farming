import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import matplotlib.dates as mdates

def plot_temp(df):
    
    temp=df.get("Temperature_C")
    date=df.get("DateTime")
   
    
    ax = plt.gca()
    ax.plot(date,temp,marker="o")

    unique_dates = pd.to_datetime(
        df['DateTime'].dt.date.unique()
    )

    ax.set_xticks(unique_dates)
    ax.set_xticklabels(
        [d.strftime('%d-%m-%Y') for d in unique_dates],

        ha='right'
    )

    ax.set_xlabel("Date")
    ax.set_ylabel("Temperature in C")
    
    plt.show()
def plot_pH(df):
    
    pH=df.get("pH")
    date=df.get("DateTime")
   
    
    ax = plt.gca()
    ax.plot(date,pH,marker="o")

    unique_dates = pd.to_datetime(
        df['DateTime'].dt.date.unique()
    )

    ax.set_xticks(unique_dates)
    ax.set_xticklabels(
        [d.strftime('%d-%m-%Y') for d in unique_dates],

        ha='right'
    )

    ax.set_xlabel("Date")
    ax.set_ylabel("pH")
    
    plt.show()
def plot_orp(df):
    
    orp=df.get("ORP_mV")
    date=df.get("DateTime")
    
    
    ax = plt.gca()
    ax.plot(date,orp,marker="o")

    unique_dates = pd.to_datetime(
        df['DateTime'].dt.date.unique()
    )

    ax.set_xticks(unique_dates)
    ax.set_xticklabels(
        [d.strftime('%d-%m-%Y') for d in unique_dates],

        ha='right'
    )

    ax.set_xlabel("Date")
    ax.set_ylabel("ORP in mV")
    
    plt.show()

def plot_tds(df):
    
    tds=df.get("TDS_ppm")
    date=df.get("DateTime")
  
    
    ax = plt.gca()
    ax.plot(date,tds,marker="o")
    
    unique_dates = pd.to_datetime(
        df['DateTime'].dt.date.unique()
    )

    ax.set_xticks(unique_dates)
    ax.set_xticklabels(
        [d.strftime('%d-%m-%Y') for d in unique_dates],

        ha='right'
    )
  
    ax.set_xlabel("Date")
    ax.set_ylabel("TDS in ppm")
    
    plt.show()

def get_file():
    df = pd.read_csv("test3.csv")
   
    return df

def main():
    df = get_file()
    df['DateTime'] = pd.to_datetime(df['DateTime'],format='%d-%m-%Y %H:%M')
    df = df.sort_values('DateTime')
    df_filtered = df[~(df['Temperature_C']==85)]
    plot_temp(df_filtered)
    plot_tds(df_filtered)
    plot_orp(df)
    plot_pH(df)
    
if __name__=="__main__":
    print("here")
    main()
