# Design Ledger Flow

Ledger Flow is a high performance position management system. It is designed around the accounting principle of a double
entry ledger, where every transaction is recorded as both a debit and a credit. 
This ensures that the ledger remains balanced and that all transactions are properly accounted for. 

The systems lofty goal is to provide a unified system for uniting back office, middle office and front office functions, 
by providing a single source of truth for all collateral movements and PnL calculations in a highly auditable manner with 
exceptional performance for latency and throughput. Whether this will be achieved is yet to be seen, nevertheless,
one can dream.

Accounting semantics fit naturally with the needs of a trading system, where there are multiple books, traders, and positions that need to be tracked and managed.
By treating books as accounts we can control how collateral is allocated, the limits for each book and ultimately the risk profile for each book. 
By treating traders as accounts we can control how much collateral they have available to trade with, and ensure that 
they are meeting their collateral requirements for their open positions. By treating positions as accounts we can track 
the unrealised PnL for each position and ensure that the collateral requirements for each position are properly accounted for.
Thus, at any one time we can hae a clear and accurate view of the PnL and collateral for all traders and books, 
which is essential for managing risk and making informed trading decisions as well as managing cashflow in and out for backoffice accounting.

## Invariants of the System - stuff that must always be true

1. The total collateral in the system must sum to zero. Meaning, that a debit cannot be created without a corresponding credit, and vice versa. 
This ensures that the ledger remains balanced and that there are no discrepancies in the collateral accounting. Thus,
the view of PnL and collateral for all traders and books will always be accurate and consistent with the actual state of the system.
2. Collateral can only be moved between accounts through defined events (e.g., trading events, admin events). 
This ensures that all collateral movements are properly tracked and accounted for, and that there
3. Collateral movements are deterministic defined by the posting rules for each trading event.
4. Auditable at all times - every ledger update must be traceable to a specific event. No updates. Append only. A real world use case of this is fraud
prevention e.g. when a trader is trying to distort their PnL; meaning on a "win" it goes to their personal book, and on a "loss" it goes
to the pods book. In the real world it would look like the trader is consistently profitable, when in reality they are not,
which is tattermount to fraud. With pre-defined posting rules and an append only ledger, this sort of fraudlent 
behavior is impossible by design. Furthermore, if a manual transfer is made the audit trail shows where it came from and where it went.
As such the fraudulent behavior would be easily detectable and traceable, and the trader could be held accountable for their actions.
Because of this we can issue a SYSTEM_CORRECTION_EVENT to correct the balances and ultimately the true PnL for the trader.
5. Append only Journal and Journal Lines - ensuring monotic sequences, easy to replay and all actions are traceable.
6. Federated access to each action. Only those authorized with the correct permisssion levels can perform certain actions.
Operators are only the actors that can envoke admin events, while traders can only envoke trading events.
An operator can also envoke trading events, as this is necessary upon settlement of trades.
7. Idempotency - each event should be idempotent, meaning that if the same event is processed multiple times, it will 
not result in duplicate or inconsistent ledger entries. This is important for ensuring the integrity of the ledger and 
preventing errors or discrepancies in the collateral accounting.
8. A book can trade multiple products, on multiple venues but all positions for a given product and venue must be in the same book. 
This is important for ensuring that the collateral requirements for each position are properly accounted for, and that 
the risk profile for each book is accurately reflected in the ledger.
9. Support futures, options and spot trading. This is important for ensuring that the ledger can accommodate a wide range of 
trading strategies and products, and that the collateral requirements for each position are properly accounted for 
based on the specific characteristics of each product type.

## Trading Events

1. **New Order**: When an algo or trader wants to open a new position, they will check with ledger flow to see if they have 
enough collateral to open the position. IF they do, then a transfer between the traders tradable balance and the
control account will be made. This will "reserve" the collateral for the position.
2. **Order Confirmed**: Once the order is confirmed and the position is now open, the "reservered" collateral will be moved
from the control account to the unrealised position account.
3. **Order Cancellation In Position**: If the order is cancelled before it is filled, then this will trigger a transfer from the 
unrealised position account back to the control account
4. **Order Filled**: When the order is filled, the collateral will be moved from the unrealised position account to the 
traders tradable balance account. This will "free up" the collateral for the trader to use for other positions or to withdraw.
5. **Order Partially Filled**: If the order is partially filled, then a proportional amount of collateral will be moved from the unrealised position account to the
traders tradable balance account. This will "free up" the collateral for the trader to use for other positions or to withdraw.
6. **Order Rejected**: If the order is rejected, then this will trigger a transfer from the control account back to 
the traders tradable balance account, as the collateral that was reserved for the order will now be released.
7. **Order Expired**: If the order expires, then this will trigger a transfer from the control account back to 
the traders tradable balance account, as the collateral that was reserved for the order will now be released.
8. **System Correction Event**: This will allow the admin to make manual adjustments to the ledger in case of discrepancies or errors.
Or upon settlement of a trade with an external counterparty, where the PnL needs to be updated for the trader and the books.

## Admin Events

1. **Deposit Book Collateral**: This will allow the admin to deposit collateral into a book's tradable balance account.
2. **Withdraw Book Collateral**: This will allow the admin to withdraw collateral from a book's tradable balance account.
3. **Transfer Collateral Between Books**: This will allow the admin to transfer collateral between books in the same control account. 
This is useful for managing collateral across different books and ensuring that each book has enough collateral to support its open positions.
4. **New Book**
5. **Book Posting Rules Update**
6. **Remove Book**
7. **Adjust Book Limits**: This will allow the admin to adjust the collateral limits for a book. 
This is useful for managing risk and ensuring that each book has appropriate collateral requirements based on its 
trading activity and risk profile.


## Account Types:

| Name              |   Type    |
|:------------------|:---------:|
| TradableBalance   | Liability |
| PreTradeControl   |   True    |
| InPosition        | Liability |
| Exchange Clearing |   False   |
| Exchange Fees     |  Expense  | 


## State Machine:

```mermaid
stateDiagram
```

## Data Model

```mermaid
erDiagram

```

## Architecture

The system will be a layered archetecture, where we have a socket server that recieves eveents from the clients (traders and algos) 
A validation layer - a fast in-memory database that holds the current state of the system, and is used to validate incoming events 
and to calculate the required collateral for each event based on the posting rules. The in memory reprsentation will then 
be written to an append only write ahead log, which will be the source of truth for the system. 
The write ahead log will be used to update the in-memory state and to project the state to a Postgres database for auditing and reporting purposes.
As such, the system will be single threaded with a single writer, meaning that all events will be processed sequentially by a single thread.

## Components

### API Layer

#### Posting Layer

#### Admin Layer

### Business Logic Layer

### Persistence Layer

### Integration with market data 

needed to calculate live PnL and to update the collateral requirements for open positions.

## Flows

## Scope:

Scope will be focused on the core functionality of the ledger flow for the MVP. Meaning focus on robust handling of 
trading events and admin events, ensuring the integrity of the ledger and the correct flow of collateral between accounts.

### In Scope:

- Handling of trading events (new order, order confirmation, order cancellation, order filled, etc.)
- Handling of admin events (deposit/withdraw collateral, new book creation, book posting rules update
- Basic API endpoints for interacting with the ledger flow (e.g., checking collateral, initiating transfers, etc.)
- Baseline perfomance and scalability considerations for handling a high volume of trading events and admin events.

### Out of Scope:

- Bankking, settlement and payment integration 
- Exchange connectivity and order routing
- Risk management and margin calls
- User interface and experience design
- Compliance and regulatory reporting

## Open Questions:

- What are the performance quotas? What is acceptable latencies?
- What are the deployment methods?


